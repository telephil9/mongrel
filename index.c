#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
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
	Mreply,
	Mreplyall,
	Mforward,
	Mdelete,
};

char *menustr[] =
{
	"reply",
	"reply all",
	"forward",
	"delete",
	nil
};

Menu menu =
{
	menustr
};

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

void
indexresetsel(void)
{
	sel = 0;
	offset = 0;
}

void
indexswitch(Mailbox *mb)
{
	indexresetsel();
	mbox = mb;
}


Message*
messageat(int index)
{
	index = mbox->count - index - 1;
	return mbox->list->elts[index];
}

void
drawmessage(Message *m, Point p, int selected)
{
	const Rune *ellipsis = L"…";
	Image *fg, *bg;
	char *s, buf[9];
	char n, r;
	Tm t;
	Rune rn;
	int i, w;
	Point pe;

	bg = cols[HIGH];
	fg = cols[TEXT];
	if(selected)
		draw(screen, Rect(p.x, p.y-Padding/2, p.x+Dx(viewr), p.y+lineh-Padding/2), bg, nil, ZP);
	n = m->flags&Fseen?' ':'N';
	r = m->flags&Fanswered ? 'R':' ';
	snprint(buf, sizeof buf, "[%c%c] ", n, r);
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
	Point p;
	int i, h, y;

	draw(screen, viewr, cols[BACK], nil, ZP);
	draw(screen, scrollr, cols[SCRL], nil, ZP);
	h = ((double)nlines/mbox->count) * Dy(scrollr);
	y = ((double)offset/mbox->count) * Dy(scrollr);
	if(h < Scrollminh)
		h = Scrollminh;
	scrposr = Rpt(addpt(scrollr.min, Pt(0,y)), addpt(scrollr.min, Pt(Dx(scrollr)-1, y+h)));
	draw(screen, scrposr, cols[BACK], nil, ZP);
	p = addpt(listr.min, Pt(Padding, Padding));
	for(i = offset; i < offset + nlines; i++){
		if(i >= mbox->list->nelts)
			break;
		drawmessage(messageat(i), p, i == sel);
		p.y += lineh;
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
	return viewr;
}

void
indexinit(Channel *c0, Channel *c1, Theme *theme)
{
	Rectangle r;

	sel = 0;
	offset = 0;
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
scroll(int Δ, int ssel)
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
	if(ssel){
		if(Δ > 0)
			sel = 0;
		else
			sel = nlines - 1;
	}
	indexdrawsync();
}

void
changesel(int Δ)
{
	if(Δ < 0 && sel == 0)
		return;
	if(Δ > 0 && sel == mbox->count - 1)
		return;
	sel += Δ;
	indexdrawsync();
}

void
setsel(Point p)
{
	int n;

	n = (p.y-listr.min.y)/lineh;
	sel = n+offset;
	indexdrawsync();
}

void
indexmouse(Mouse m)
{
	int sl;

	if(!ptinrect(m.xy, viewr))
		return;
	if(m.buttons & 1){
		setsel(m.xy);
		sendp(selc, messageat(sel));
	}else if(m.buttons & 2){
		/* TODO: menu */
	}else if(m.buttons & 4){
		setsel(m.xy);
		sendp(showc, messageat(sel));
	}else if(m.buttons & 8){
		sl = mousescrollsize(nlines);
		scroll(-sl, 0);
	}else if(m.buttons & 16){
		sl = mousescrollsize(nlines);
		scroll(sl, 0);
	}
}

void
indexkey(Rune k)
{
	switch(k){
		case Kup:
			if(sel == offset)
				scroll(-nlines, 1);
			else if(sel > offset)
				changesel(-1);
			sendp(selc, messageat(sel));
			break;
		case Kdown:
			if(sel < (mbox->count - 1)){
				if(sel == offset + nlines - 1){
					sel = offset + nlines;
					scroll(nlines, 0);
				}else
					changesel(1);
				sendp(selc, messageat(sel));
			}
			break;
		case '\n':
			sendp(showc, messageat(sel));
			break;
		case Kpgup:
			if(sel > 0){
				sel -= nlines;
				if(sel < 0)
					sel = 0;
				scroll(-nlines, 0);
				sendp(selc, messageat(sel));
			}
			break;
		case Kpgdown:
			if(sel < (mbox->count - 1)){
				sel += nlines;
				if(sel >= mbox->count)
					sel = mbox->count - 1;
				scroll(nlines, 0);
				sendp(selc, messageat(sel));
			}
			break;
		case Khome:
			sel = 0;
			scroll(-mbox->count, 0);
			sendp(selc, messageat(sel));
			break;
		case Kend:
			sel = mbox->count - 1;
			scroll(mbox->count, 0);
			sendp(selc, messageat(sel));
			break;
	}
}

