#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#include <plumb.h>
#include "theme.h"
#include "kbd.h"
#include "a.h"
#include "w.h"

enum
{
	Padding = 4,
};

typedef struct Handler Handler;

struct Handler
{
	char *type;
	char *fmt;
	char *dst;
} handlers[] = {
	"text/html", "file://%s/body.html", "web",
	"image/png", "%s/body.png", "image",
	"image/x-png", "%s/body.png", "image",
	"image/jpeg", "%s/body.jpg", "image",
	"image/jpg", "%s/body.jpg", "image",
	"image/gif", "%s/body.gif", "image",
	"image/bmp", "%s/body.bmp", "image",
	"image/tiff", "%s/body.tiff", "image",
	"image/p9bit", "%s/body", "image",
	"application/pdf", "%s/body.pdf", "postscript",
	"application/postscript", "%s/body.ps", "postscript",
};

static Mousectl *mc;
static Kbdctl *kc;
static Text text;
static Rectangle viewr;
static Rectangle headr;
static Rectangle partsr;
static Rectangle textr;
static Image *cols[NCOLS];
static Image *headercol;
static Message *mesg;
static Tzone *tz;
static int showcc;
static Message *parts[16];
static int nparts;

char*
findtextpart(Message *m)
{
	Message *p;
	int i;

	if(strncmp(m->type, "multipart/", 10) == 0){
		for(i = 0; i < m->parts->nelts; i++){
			p = m->parts->elts[i];
			if(strcmp(p->type, "text/plain") == 0)
				return p->body;
			else if(strncmp(p->type, "multipart/", 10) == 0)
				return findtextpart(p);
		}
	}
	return m->body;
}

void
collectparts(Message *m)
{
	Message *p;
	int i;

	if(m->parts == nil)
		return;
	for(i = 0; i < m->parts->nelts; i++){
		p = m->parts->elts[i];
		if(strncmp(p->type, "multipart/", 10) == 0)
			collectparts(p);
		else{
			if(strcmp(p->type, "text/plain") == 0 && strncmp(p->disposition, "file", 4) != 0)
				continue;
			parts[nparts++] = p;
		}
	}
}

void
pagershow(Message *m)
{
	char *body;
	int needresize;
	int oldnparts;

	oldnparts = nparts;
	needresize = 0;
	mesg = m;
	if(showcc){
		if(mesg->cc == nil || mesg->cc[0] == 0){
			showcc = 0;
			needresize = 1;
		}
	}else{
		if(mesg->cc != nil && mesg->cc[0] != 0){
			showcc = 1;
			needresize = 1;
		}
	}
	mesgloadbody(mesg);
	nparts = 0;
	collectparts(mesg);
	if(nparts != oldnparts)
		needresize = 1;
	if(needresize)
		pagerresize(viewr);
	body = findtextpart(mesg);
	textset(&text, body, strlen(body));
	pagerdraw();
}

static
Point
drawheader(Point p, char *h, char *s)
{
	Rune r;
	
	p = string(screen, p, cols[BORD], ZP, font, h);
	while(s && *s){
		s += chartorune(&r, s);
		p = runestringn(screen, p, cols[TEXT], ZP, font, &r, 1);
	}
	return p;
}

Point
drawparts(Point p)
{
	Point q;
	int i;

	for(i = 0; i < nparts; i++){
//		q = string(screen, p, cols[TEXT], ZP, font, "=> ");
		q = p;
		if(parts[i]->filename != nil && parts[i]->filename[0] != 0)
			q = string(screen, q, cols[TEXT], ZP, font, parts[i]->filename);
		else
			q = string(screen, q, cols[TEXT], ZP, font, "<no description>");
		q = string(screen, q, cols[BORD], ZP, font, " [");
		q = string(screen, q, cols[BORD], ZP, font, parts[i]->type);
		string(screen, q, cols[BORD], ZP, font, "]");
		p.y += font->height + Padding;
	}
	return p;
}

void
pagerdraw(void)
{
	Point p, q;
	char buf[32], *s;
	Rune r;
	Tm t;
	int w;

	draw(screen, viewr, cols[BACK], nil, ZP);
	if(mesg != nil){
		p = addpt(headr.min, Pt(Padding, Padding));
		tmtime(&t, mesg->time, tz);
		snprint(buf, sizeof buf, "%τ", tmfmt(&t, "DD/MM/YYYY hh:mm"));
		w = stringwidth(font, buf);
		string(screen, addpt(p, Pt(Dx(headr) - w - 2*Padding, 0)), cols[TEXT], ZP, font, buf);
		q = drawheader(p, "   From ", mesg->sender);
		if(strcmp(mesg->sender, mesg->from) != 0){
			q = string(screen, q, cols[TEXT], ZP, font, " <");
			s = mesg->from;
			while(s && *s){
				s += chartorune(&r, s);
				q = runestringn(screen, q, cols[TEXT], ZP, font, &r, 1);
			}
			string(screen, q, cols[TEXT], ZP, font, ">");
		}
		p.y += font->height + Padding;
		drawheader(p, "     To ", mesg->to);
		if(showcc){
			p.y += font->height + Padding;
			drawheader(p, "     Cc ", mesg->cc);
		}
		p.y += font->height + Padding;
		drawheader(p, "Subject ", mesg->subject);
	}
	line(screen, addpt(headr.min, Pt(0, Dy(headr))), headr.max, 0, 0, 0, headercol, ZP);
	if(nparts > 0){
		p = addpt(partsr.min, Pt(Padding, Padding));
		drawparts(p);
		line(screen, addpt(partsr.min, Pt(0, Dy(partsr))), partsr.max, 0, 0, 0, headercol, ZP);
	}
	textdraw(&text);
}

void
pagerresize(Rectangle r)
{
	int n;

	n = showcc ? 4 : 3;
	viewr = r;
	headr = viewr;
	headr.max.y = headr.min.y + Padding + n*(font->height + Padding);
	if(nparts > 0){
		partsr = viewr;
		partsr.min.y = headr.max.y + 1;
		partsr.max.y = partsr.min.y + nparts*(font->height + Padding) + Padding;
	}
	textr = viewr;
	if(nparts > 0)
		textr.min.y = partsr.max.y + 1;
	else
		textr.min.y = headr.max.y + 1;
	textresize(&text, textr);
}

void
savepart(Message *m)
{
	char name[255] = {0}, iname[255] = {0}, buf[1024] = {0};
	int ifd, fd, n;

	snprint(name, sizeof name, "%s", m->filename);
	n = kbdenter("Save as:", name, sizeof name, mc, kc, nil);
	if(n <= 0)
		return;
	fd = create(name, OWRITE, 0644);
	if(fd < 0){
		fprint(2, "unable to save part as '%s': %r", name); /* FIXME */
		return;
	}
	snprint(iname, sizeof iname, "%s/body", m->path);
	ifd = open(iname, OREAD);
	if(ifd < 0){
		close(fd);
		fprint(2, "unable to open part file '%s': %r", iname); /* FIXME */
		return;
	}
	for(;;){
		n = read(ifd, buf, sizeof buf);
		if(n <= 0)
			break;
		write(fd, buf, n);
	}
	close(ifd);
	close(fd);
}

void
plumbpart(Message *m)
{
	char buf[1024] = {0};
	int fd, n;

	fd = plumbopen("send", OWRITE);
	if(fd < 0)
		return;
	for(n = 0; n < nelem(handlers); n++){
		if(strcmp(m->type, handlers[n].type) == 0){
			snprint(buf, sizeof buf, handlers[n].fmt, m->path);
			plumbsendtext(fd, "mongrel", handlers[n].dst, nil, buf);
			close(fd);
			return;
		}
	}
	snprint(buf, sizeof buf, "%s/body", m->path);
	plumbsendtext(fd, "mongrel", nil, nil, buf);
	close(fd);
}

void
partclick(Mouse m)
{
	enum { Mpsave, Mpplumb };
	char *menustr[] = { "save", "plumb", nil };
	Menu menu = { menustr };
	int n, i;

	n = (m.xy.y - partsr.min.y) / (font->height+Padding);
	if(n < 0 || n >= nparts)
		return;
	if(m.buttons == 2){
		i = menuhit(2, mc, &menu, nil);
		switch(i){
			case Mpsave:
				savepart(parts[n]);
				break;
			case Mpplumb:
				plumbpart(parts[n]);
				break;
		}
	}else if(m.buttons == 4)
		plumbpart(parts[n]);
}

void
pagermouse(Mouse m)
{
	if(!ptinrect(m.xy, viewr))
		return;
	if(nparts > 0 && ptinrect(m.xy, partsr))
		partclick(m);
	else if(ptinrect(m.xy, textr))
		textmouse(&text, mc);
}

void
pagerkey(Rune k)
{
	textkeyboard(&text, k);
}

void
pagerinit(Mousectl *mctl, Kbdctl *kctl, Theme *theme)
{
	Rectangle r;

	mc = mctl;
	kc = kctl;
	tz = tzload("local");
	showcc = 0;
	nparts = 0;
	if(theme != nil){
		cols[BACK] = theme->back;
		cols[BORD] = theme->border;
		cols[TEXT] = theme->text;
		cols[HTEXT] = theme->htext;
		cols[HIGH] = theme->high;
		headercol = theme->title;
	}else{
		r = Rect(0, 0, 1, 1);
		cols[BACK] = display->white;
		cols[BORD] = allocimage(display, r, screen->chan, 1, 0x999999FF);
		cols[TEXT] = allocimage(display, r, screen->chan, 1, 0x000000FF);
		cols[HTEXT] = allocimage(display, r, screen->chan, 1, 0x000000FF);
		cols[HIGH] = allocimage(display, r, screen->chan, 1, 0xCCCCCCFF);
		headercol = allocimage(display, r, screen->chan, 1, DGreygreen);
	}
	textinit(&text, screen, screen->r, font, cols);
}

