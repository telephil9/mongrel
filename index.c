#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#include <plumb.h>
#include "theme.h"
#include "a.h"

enum
{
	Padding = 4,
	Scrollwidth = 12,
	Scrollgap = 2,
	Scrollminh = 5,
	Collapsedlines = 10,
};

enum
{
	BACK,
	TEXT,
	HIGH,
	SCRL,
	NCOLS,
};

enum
{
	Mplumb,
	Mdelete,
};

char *menustr[] =
{
	"plumb",
	"delete",
	nil
};

Menu menu =
{
	menustr
};

Mousectl *mctl;
Channel *showc;
Channel *selc;
Mailbox *mbox;
static Image *cols[NCOLS];
static Rectangle viewr;
static Rectangle listr;
static Rectangle scrollr;
static int nlines;
static int offset;
static int sel;
static int lineh;

Message*
messageat(int index)
{
	index = mbox->count - index - 1;
	return mbox->list->elts[index];
}

void
indexadded(int index)
{
	index = mbox->count - index - 1;
	if(sel <= index)
		++sel;
}

void
indexremoved(int index)
{
	index = mbox->count - index - 1;
	if(sel >= index){
		--sel;
		if(sel < 0)
			sel = 0;
		sendp(selc, messageat(sel));
	}
}

void
indexswitch(Mailbox *mb)
{
	sel = 0;
	offset = 0;
	mbox = mb;
}

void
ensureselvisible(void)
{
	int n;

	if(sel > offset && sel < offset + nlines)
		return;
	offset = nlines*(sel/nlines);
	n = mbox->list->nelts;
	if(offset + n%nlines >= n)
		offset = n - n%nlines;
}

Rectangle
messagerect(int index)
{
	Point p, q;
	int n;

	n = index - offset;
	p = addpt(listr.min, Pt(Padding, n*lineh + Padding));
	q = Pt(listr.max.x, p.y + lineh);
	return Rpt(p, q);
}

void
drawmessage(int index, int selected)
{
	const Rune *ellipsis = L"…";
	Message *m;
	Image *fg, *bg;
	char *s, buf[9], n, r;
	Tm t;
	Rune rn;
	int i, w;
	Rectangle lr;
	Point p, pe;

	lr = messagerect(index);
	bg = selected ? cols[HIGH] : cols[BACK];
	fg = cols[TEXT];
	draw(screen, rectsubpt(lr, Pt(0, Padding/2)), bg, nil, ZP);
	m = messageat(index);
	n = m->flags&Fseen?' ':'N';
	r = m->flags&Fanswered ? 'R':' ';
	snprint(buf, sizeof buf, "[%c%c] ", n, r);
	p = lr.min;
	p = string(screen, p, fg, ZP, font, buf);
	tmtime(&t, m->time, nil);
	snprint(buf, sizeof buf, "%τ", tmfmt(&t, "DD/MM/YY"));
	p = string(screen, p, fg, ZP, font, buf);
	p = string(screen, p, fg, ZP, font, "  ");
	s = m->sender;
	pe = addpt(p, Pt(20*stringwidth(font, " "), 0));
	for(i = 0; i < 20; i++){
		if(*s == '@')
			s = "";
		if(*s && i == 19){
			p = runestringn(screen, p, fg, ZP, font, ellipsis, 1);
			break;
		}else if(*s){
			s += chartorune(&rn, s);
			p = runestringn(screen, p, fg, ZP, font, &rn, 1);
		}else
			p = stringn(screen, p, fg, ZP, font, " ", 1);
	}
	p = string(screen, pe, fg, ZP, font, "  ");
	s = m->subject;
	while(s && *s){
		s += chartorune(&rn, s);
		w = runestringnwidth(font, &rn, 1);
		if(p.x + w + 2*Padding > viewr.max.x){
			runestringn(screen, p, fg, ZP, font, ellipsis, 1);
			break;
		}
		p = runestringn(screen, p, fg, ZP, font, &rn, 1);
	}
}

void
indexdraw(void)
{
	Rectangle scrposr;
	int i, h, y;

	draw(screen, viewr, cols[BACK], nil, ZP);
	draw(screen, scrollr, cols[SCRL], nil, ZP);
	h = ((double)nlines/mbox->count) * Dy(scrollr);
	y = ((double)offset/mbox->count) * Dy(scrollr);
	if(h < Scrollminh)
		h = Scrollminh;
	scrposr = Rpt(addpt(scrollr.min, Pt(0,y)), addpt(scrollr.min, Pt(Dx(scrollr)-1, y+h)));
	draw(screen, scrposr, cols[BACK], nil, ZP);
	for(i = offset; i < offset + nlines; i++){
		if(i >= mbox->list->nelts)
			break;
		drawmessage(i, i == sel);
	}
}

void
indexdrawsync(void)
{
	indexdraw();
	flushimage(display, 1);
}

Rectangle
indexresize(Rectangle r, int collapsed)
{
	lineh = font->height + Padding;
	viewr = r;
	if(collapsed)
		viewr.max.y = viewr.min.y + Collapsedlines * lineh + Padding;
	scrollr = viewr;
	scrollr.max.x = scrollr.min.x + Scrollwidth + Scrollgap;
	scrollr = insetrect(scrollr, 1);
	listr = viewr;
	listr.min.x += Scrollwidth + Scrollgap;
	listr.max.x -= Padding;
	nlines = Dy(viewr) / lineh;
	ensureselvisible();
	return viewr;
}

void
indexinit(Mousectl *mc, Channel *c0, Channel *c1, Theme *theme)
{
	Rectangle r;

	sel = 0;
	offset = 0;
	mctl = mc;
	showc = c0;
	selc = c1;
	if(theme != nil){
		cols[BACK] = theme->back;
		cols[TEXT] = theme->text;
		cols[HIGH] = theme->border;
		cols[SCRL] = theme->border;
	}else{
		r = Rect(0, 0, 1, 1);
		cols[BACK] = display->white;
		cols[TEXT] = display->black;
		cols[HIGH] = allocimage(display, r, screen->chan, 1, 0xCCCCCCFF);
		cols[SCRL] = allocimage(display, r, screen->chan, 1, 0x999999FF);
		/*
		cols[BACK] = allocimage(display, r, screen->chan, 1, 0x282828FF);
		cols[TEXT] = allocimage(display, r, screen->chan, 1, 0xA89984FF);
		cols[HIGH] = allocimage(display, r, screen->chan, 1, 0x3C3836FF);
		cols[SCRL] = allocimage(display, r, screen->chan, 1, 0x504945FF);
		*/
	}
}

void
scroll(int Δ)
{
	int nelts;

	nelts = mbox->list->nelts;
	if(nelts <= nlines)
		return;
	if(Δ < 0 && offset == 0)
		return;
	if(Δ > 0 && offset + nlines >= nelts)
		return;
	offset += Δ;
	if(offset < 0)
		offset = 0;
	if(offset + nelts%nlines >= nelts)
		offset = nelts - nelts%nlines;
	indexdrawsync();
}

void
select(int newsel, Channel *c)
{
	if(newsel < 0)
		newsel = 0;
	if(newsel >= mbox->count)
		newsel = mbox->count - 1;
	if(newsel == sel)
		return;
	if(newsel < offset || newsel >= offset + nlines){
		sel = newsel;
		ensureselvisible();
		indexdraw();
	}else{
		drawmessage(sel, 0);
		drawmessage(newsel, 1);
		sel = newsel;
	}
	flushimage(display, 1);
	sendp(c, messageat(sel));
}

static
int
indexat(Point p)
{
	return offset + (p.y-listr.min.y)/lineh;
}

static
void
plumbmsg(Message *m)
{
	int fd;

	fd = plumbopen("send", OWRITE|OCEXEC);
	if(fd<0)
		return;
	plumbsendtext(fd, "mongrel", nil, nil, m->path);
	close(fd);	
}

void
mesgmenuhit(int but, Mouse m)
{
	int n;

	n = menuhit(but, mctl, &menu, nil);
	switch(n){
	case Mplumb:
		select(indexat(m.xy), selc);
		plumbmsg(messageat(sel));
		break;
	case Mdelete:
		select(indexat(m.xy), selc);
		mesgdel(mbox, messageat(sel));
		break;
	}
}

void
indexmouse(Mouse m)
{
	int n;

	if(!ptinrect(m.xy, viewr))
		return;
	if(ptinrect(m.xy, listr)){
		if(m.buttons & 1){
			select(indexat(m.xy), selc);
		}else if(m.buttons & 2){
			mesgmenuhit(2, m);
		}else if(m.buttons & 4){
			n = indexat(m.xy);
			if(n != sel)
				select(indexat(m.xy), showc);
			else
				sendp(showc, messageat(sel));
		}else if(m.buttons & 8){
			n = mousescrollsize(nlines);
			scroll(-n);
		}else if(m.buttons & 16){
			n = mousescrollsize(nlines);
			scroll(n);
		}
	}else if(ptinrect(m.xy, scrollr)){
		if(m.buttons & 1){
			n = (m.xy.y - scrollr.min.y) / lineh;
			scroll(-n);
		}else if(m.buttons & 2){
			offset = (m.xy.y - scrollr.min.y) * mbox->list->nelts / Dy(scrollr);
			indexdrawsync();
		}else if(m.buttons & 4){
			n = (m.xy.y - scrollr.min.y) / lineh;
			scroll(n);
		}
	}
}


void
indexkey(Rune k)
{
	switch(k){
	case Kup:
		select(sel - 1, selc);
		break;
	case Kdown:
		select(sel + 1, selc);
		break;
	case Kpgup:
		select(sel - nlines, selc);
		break;
	case Kpgdown:
		select(sel + nlines, selc);
		break;
	case Khome:
		select(0, selc);
		break;
	case Kend:
		select(mbox->count - 1, selc);
		break;
	case '\n':
		sendp(showc, messageat(sel));
		break;
	}
}

