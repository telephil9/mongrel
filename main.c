#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#include <plumb.h>
#include "theme.h"
#include "kbd.h"
#include "a.h"

enum
{
	Padding = 4,
};

enum
{
	Emouse,
	Eresize,
	Ekeyboard,
	Eseemail,
	Eshowmesg,
	Eselmesg,
};

enum
{
	BACK,
	TEXT,
	BORD,
	PBRD,
	PBCK,
	NCOLS,
};

Mousectl *mctl;
Kbdctl *kctl;
Channel *showc;
Channel *selc;
Channel *eventc;
Channel *loadc;
Mailbox *mboxes[16];
int nmboxes;
char *mbmenustr[16] = {0};
Menu mbmenu = { mbmenustr };
Mailbox *mbox;
static Image *cols[NCOLS];
Rectangle headr;
Rectangle indexr;
Rectangle pagerr;
int collapsed = 0;

void
drawheader(void)
{
	char buf[255] = {0};
	Point p;

	draw(screen, headr, cols[BACK], nil, ZP);
	p = headr.min;
	p.x += Padding;
	p.y += Padding / 2;
	if(mbox->unseen > 0)
		snprint(buf, sizeof buf, "» %s [total:%d - new:%d]", mbox->name, mbox->count, mbox->unseen);
	else
		snprint(buf, sizeof buf, "» %s [total:%d]", mbox->name, mbox->count);
	string(screen, p, cols[TEXT], ZP, font, buf);
	line(screen, Pt(headr.min.x, headr.max.y), headr.max, 0, 0, 0, cols[BORD], ZP);
}

void
redraw(void)
{
	draw(screen, screen->r, cols[BACK], nil, ZP);
	drawheader();
	indexdraw();
	if(collapsed){
		line(screen, Pt(indexr.min.x, indexr.max.y), indexr.max, 0, 0, 0, cols[BORD], ZP);
		pagerdraw();
	}
	flushimage(display, 1);
}

void
resize(void)
{
	headr = screen->r;
	headr.max.y = headr.min.y+Padding+font->height;
	indexr = screen->r;
	indexr.min.y = headr.max.y + 1;
	indexr = indexresize(indexr, collapsed);
	if(collapsed){
		pagerr = screen->r;
		pagerr.min.y = indexr.max.y + 1;
		pagerresize(pagerr);
	}
	redraw();
}

void
loadproc(void *v)
{
	Channel *c;

	c = v;
	mboxload(mbox, c);
}

void
drawprogress(Rectangle r, Rectangle pr)
{
	draw(screen, pr, cols[PBCK], nil, ZP);
	border(screen, r, 2, cols[PBRD], ZP);	
	flushimage(display, 1);
}

void
loadmbox(void)
{
	ulong total, count, n;
	Point sp, p;
	Rectangle r, pr;
	int pc;
	char buf[255] = {0};

	draw(screen, screen->r, cols[BACK], nil, ZP);
	p.x = (Dx(screen->r)-200)/2;
	p.y = (Dy(screen->r)-25)/2;
	r = rectaddpt(rectaddpt(Rect(0, 0, 200, 25), p), screen->r.min);
	snprint(buf, sizeof buf, "Loading %s...", mbox->name);
	sp.x = (Dx(screen->r)-stringwidth(font, buf))/2;
	sp.y = r.min.y - font->height - 12 - screen->r.min.y;
	string(screen, addpt(screen->r.min, sp), cols[TEXT], ZP, font, buf);
	replclipr(screen, 1, insetrect(r, -2));
	loadc = chancreate(sizeof(ulong), 1);
	proccreate(loadproc, loadc, 8192);
	total = recvul(loadc);
	count = 0;
	for(;;){
		n = recvul(loadc);
		if(n == 0)
			break;
		count += 1;
		pc = 200*((double)count/total);
		pr = rectaddpt(rectaddpt(Rect(0, 0, pc, 25), p), screen->r.min);
		if(count % (total/100) == 0)
			drawprogress(r, pr);
	}
	chanfree(loadc);
	replclipr(screen, 1, screen->r);
}

void
switchmbox(int n)
{
	if(mbox==mboxes[n])
		return;
	mbox = mboxes[n];
	if(!mbox->loaded)
		loadmbox();
	indexswitch(mbox);
	collapsed = 0;
	resize();
}

void
mouse(Mouse m)
{
	int n;

	if(ptinrect(m.xy, headr) && m.buttons==4){
		n = menuhit(3, mctl, &mbmenu, nil);
		if(n >= 0)
			switchmbox(n);
		return;
	}
	indexmouse(m);
	if(collapsed)
		pagermouse(m);
}

void
key(Key k)
{
	if(k.k == Kdel)
		threadexitsall(nil);
	else if(k.k == 'q'){
		if(collapsed){
			collapsed = 0;
			resize();
		}else
			threadexitsall(nil);
	}else if(k.mods == Malt){
		if(collapsed)
			pagerkey(k.k);
	}else
		indexkey(k.k);
}

void
seemail(Mailevent e)
{
	Mailbox *mb;
	char *s;
	int i;

	for(mb = nil, i = 0; i < nmboxes; i++){
		if(strncmp(mboxes[i]->path, e.path, strlen(mboxes[i]->path))==0){
			mb = mboxes[i];
			break;
		}
	}
	if(mb==nil)
		return;
	s = e.path;
	switch(e.type){
		case Enew:
			i = mboxadd(mb, s);
			if(mb==mbox){
				indexadded(i);
				redraw();
			}
			break;
		case Edelete:
			i = mboxdel(mb, s);
			if(mb==mbox){
				indexremoved(i);
				redraw();
			}
			break;
		case Emodify:
			i = mboxmod(mb, s);
			if(i >= 0 && mb==mbox)
				redraw();
			break;
	}
	free(s);
}

void
init(Channel *selc)
{
	Theme *theme;
	Rectangle r;

	theme = loadtheme();
	if(theme != nil){
		cols[BACK] = theme->back;
		cols[TEXT] = theme->text;
		cols[BORD] = theme->title;
		cols[PBRD] = theme->menubord;
		cols[PBCK] = theme->menuback;
	}else{
		r = Rect(0, 0, 1, 1);
		cols[BACK] = allocimage(display, r, screen->chan, 1, 0xFFFFFFFF);
		cols[TEXT] = allocimage(display, r, screen->chan, 1, 0x000000FF);
		cols[BORD] = allocimage(display, r, screen->chan, 1, DGreygreen);
		cols[PBRD] = allocimage(display, r, screen->chan, 1, 0x88CC88FF);
		cols[PBCK] = allocimage(display, r, screen->chan, 1, 0xEAFFEAFF);
		/*
		cols[BACK] = allocimage(display, r, screen->chan, 1, 0x282828FF);
		cols[TEXT] = allocimage(display, r, screen->chan, 1, 0xA89984FF);
		cols[BORD] = allocimage(display, r, screen->chan, 1, 0x98971AFF);
		*/
	}
	indexinit(mctl, showc, selc, theme);
	pagerinit(mctl, kctl, theme);
}

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
showmsg(Message *m)
{
	if(collapsed == 0){
		collapsed = 1;
		resize();
	}
	pagershow(m);
	if(mesgmarkseen(mbox, m))
		redraw();
}

void
selchanged(Message *m)
{
	if(collapsed == 0)
		return;
	showmsg(m);
}


void
usage(void)
{
	fprint(2, "usage: %s [-m maildir]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Mouse m;
	Key k;
	Mailevent e;
	Message *msg;
	Alt alts[] = 
	{
		{ nil, &m,   CHANRCV },
		{ nil, nil,  CHANRCV },
		{ nil, &k,   CHANRCV },
		{ nil, &e,   CHANRCV },
		{ nil, &msg, CHANRCV },
		{ nil, &msg, CHANRCV },
		{ nil, nil,  CHANEND },
	};
	char *s;

	nmboxes = 0;
	ARGBEGIN{
	case 'm':
		s = EARGF(usage());
		mboxes[nmboxes] = mboxinit(s);
		mbmenustr[nmboxes] = mboxes[nmboxes]->name;
		nmboxes++;
		break;
	default:
		fprint(2, "unknown flag '%c'\n", ARGC());
		usage();
	}ARGEND
	if(nmboxes==0){
		fprint(2, "no maildir specified\n");
		usage();
	}
	tmfmtinstall();
	if(initdraw(nil, nil, "mongrel")<0)
		sysfatal("initdraw: %r");
	display->locking = 0;
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkbd()) == nil)
		sysfatal("initkbd: %r");
	if((eventc = chancreate(sizeof(Mailevent), 0))==nil)
		sysfatal("chancreate: %r");
	if((showc = chancreate(sizeof(Message*), 1))==nil)
		sysfatal("chancreate: %r");
	if((selc = chancreate(sizeof(Message*), 1))==nil)
		sysfatal("chancreate: %r");
	alts[0].c = mctl->c;
	alts[1].c = mctl->resizec;
	alts[2].c = kctl->c;
	alts[3].c = eventc;
	alts[4].c = showc;
	alts[5].c = selc;
	proccreate(seemailproc, eventc, 8192);
	init(selc);
	switchmbox(0);
	resize();
	for(;;){
		switch(alt(alts)){
		case Emouse:
			mouse(m);
			break;
		case Eresize:
			if(getwindow(display, Refnone)<0)
				sysfatal("cannot reattach: %r");
			resize();
			break;
		case Ekeyboard:
			key(k);
			break;
		case Eseemail:
			seemail(e);
			break;
		case Eshowmesg:
			showmsg(msg);
			break;
		case Eselmesg:
			//plumbmsg(msg);
			selchanged(msg);
			break;
		}
	}
}
