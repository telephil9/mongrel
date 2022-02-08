#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#include <plumb.h>
#include "w.h"

enum
{
	Scrollwidth = 12,
	Padding = 4,
};

enum
{
	Msnarf,
	Mplumb,
};
char *menu2str[] = {
	"snarf",
	"plumb",
	nil,
};
Menu menu2 = { menu2str };

void
computelines(Text *t)
{
	int i, x, w, l, c;
	Rune r;

	t->lines[0] = 0;
	t->nlines = 1;
	w = Dx(t->textr);
	x = 0;
	for(i = 0; i < t->ndata; ){
		c = chartorune(&r, &t->data[i]);
		if(r == '\n'){
			if(i + c == t->ndata)
				break;
			t->lines[t->nlines++] = i + c;
			x = 0;
		}else{
			l = 0;
			if(r == '\t'){
				x += stringwidth(t->font, "    ");
			}else{
				l = runestringnwidth(t->font, &r, 1);
				x += l;
			}
			if(x > w){
				t->lines[t->nlines++] = i;
				x = l;
			}
		}
		i += c;
	}
}

int
indexat(Text *t, Point p)
{
	int line, i, s, e, x, c, l;
	Rune r;

	if(!ptinrect(p, t->textr))
		return -1;
	line = t->offset + ((p.y - t->textr.min.y) / font->height);
	s = t->lines[line];
	if(line+1 >= t->nlines)
		e = t->ndata;
	else
		e = t->lines[line+1] - 2; /* make sure we exclude the newline */
	c = 0;
	x = t->textr.min.x;
	for(i = s; i < e; ){
		c = chartorune(&r, &t->data[i]);
		if(r == '\t')
			l = stringwidth(t->font, "    ");
		else
			l = runestringnwidth(t->font, &r, 1);
		if(x <= p.x && p.x <= x+l)
			break;
		i += c;
		x += l;
	}
	if(r == '\n')
		i -= c;
	return i;
}

void
textinit(Text *t, Image *b, Rectangle r, Font *f, Image *cols[NCOLS])
{
	memset(t, 0, sizeof *t);
	t->b = b;
	t->font = f;
	t->s0 = -1;
	t->s1 = -1;
	t->offset = 0;
	textresize(t, r);
	memmove(t->cols, cols, sizeof t->cols);
}

void
textset(Text *t, char *data, usize ndata)
{
	t->s0 = -1;
	t->s1 = -1;
	t->offset = 0;
	t->data = data;
	t->ndata = ndata;
	computelines(t);
}

void
textresize(Text *t, Rectangle r)
{
	t->r = r;
	t->vlines = Dy(t->r) / t->font->height;
	t->scrollr = rectaddpt(Rect(0, 0, Scrollwidth, Dy(r)), r.min);
	t->textr = r;
	t->textr.min.x = t->scrollr.max.x + Padding;
	if(t->nlines > 0)
		computelines(t);
}

int
selected(Text *t, int index)
{
	int s0, s1;

	if(t->s0 < 0 || t->s1 < 0)
		return 0;
	s0 = t->s0 < t->s1 ? t->s0 : t->s1;
	s1 = t->s0 > t->s1 ? t->s0 : t->s1;
	return s0 <= index && index <= s1;
}

void
drawline(Text *t, int index)
{
	int i, s, e;
	Point p;
	Rune r;
	Image *fg, *bg;

	s = t->lines[t->offset+index];
	if(t->offset+index+1 >= t->nlines)
		e = t->ndata;
	else
		e = t->lines[t->offset+index+1];
	p = addpt(t->textr.min, Pt(0, index*font->height));
	for(i = s; i < e; ){
		fg = selected(t, i) ? t->cols[HTEXT] : t->cols[TEXT];
		bg = selected(t, i) ? t->cols[HIGH]  : t->cols[BACK];
		i += chartorune(&r, &t->data[i]);
		if(r == '\n')
			if(s + 1 == e) /* empty line */
				r = L' ';
			else
				continue;
		if(r == '\t')
			p = stringbg(t->b, p, fg, ZP, t->font, "    ", bg, ZP);
		else
			p = runestringnbg(t->b, p, fg, ZP, t->font, &r, 1, bg, ZP);
	}
}

void
textdraw(Text *t)
{
	int i, h, y;
	Rectangle sr;

	draw(t->b, t->r, t->cols[BACK], nil, ZP);
	draw(t->b, t->scrollr, t->cols[BORD], nil, ZP);
	border(t->b, t->scrollr, 0, t->cols[TEXT], ZP);
	if(t->nlines > 0){
		h = ((double)t->vlines / t->nlines) * Dy(t->scrollr);
		y = ((double)t->offset / t->nlines) * Dy(t->scrollr);
		sr = Rect(t->scrollr.min.x + 1, t->scrollr.min.y + y + 1, t->scrollr.max.x - 1, t->scrollr.min.y + y + h - 1);
	}else
		sr = insetrect(t->scrollr, -1);
	draw(t->b, sr, t->cols[BACK], nil, ZP);
	for(i = 0; i < t->vlines; i++){
		if(t->offset+i >= t->nlines)
			break;
		drawline(t, i);
	}
	flushimage(display, 1);
}

static
void
scroll(Text *t, int lines)
{
	if(t->nlines <= t->vlines)
		return;
	if(lines < 0 && t->offset == 0)
		return;
	if(lines > 0 && t->offset + t->vlines >= t->nlines)
		return;
	t->offset += lines;
	if(t->offset < 0)
		t->offset = 0;
	if(t->offset + t->nlines%t->vlines >= t->nlines)
		t->offset = t->nlines - t->nlines%t->vlines;
	textdraw(t);
}

void
textkeyboard(Text *t, Rune k)
{
	switch(k){
	case Kup:
		scroll(t, -1);
		break;
	case Kdown:
		scroll(t, 1);
		break;
	case Kpgup:
		scroll(t, -t->vlines);
		break;
	case Kpgdown:
		scroll(t, t->vlines);
		break;
	case Khome:
		scroll(t, -t->nlines);
		break;
	case Kend:
		scroll(t, t->nlines);
		break;
	}
}

void
snarfsel(Text *t)
{
	int fd, s0, s1;

	if(t->s0 < 0 || t->s1 < 0)
		return;
	fd = open("/dev/snarf", OWRITE);
	if(fd < 0)
		return;
	s0 = t->s0 < t->s1 ? t->s0 : t->s1;
	s1 = t->s0 > t->s1 ? t->s0 : t->s1;
	write(fd, &t->data[s0], s1 - s0 + 1);
	close(fd);
}

void
plumbsel(Text *t)
{
	int fd, s0, s1;
	char *s;

	if(t->s0 < 0 || t->s1 < 0)
		return;
	fd = plumbopen("send", OWRITE);
	if(fd < 0)
		return;
	s0 = t->s0 < t->s1 ? t->s0 : t->s1;
	s1 = t->s0 > t->s1 ? t->s0 : t->s1;
	s = smprint("%.*s", s1 - s0 + 1, &t->data[s0]);
	plumbsendtext(fd, argv0, nil, nil, s);
	free(s);
	close(fd);
}

void
menu2hit(Text *t, Mousectl *mc)
{
	int n;

	n = menuhit(2, mc, &menu2, nil);
	switch(n){
		case Msnarf:
			snarfsel(t);
			break;
		case Mplumb:
			plumbsel(t);
			break;
	}
}

void
textmouse(Text *t, Mousectl *mc)
{
	static selecting = 0;
	Point p;
	int n;

	if(ptinrect(mc->xy, t->scrollr)){
		if(mc->buttons == 1){
			n = (mc->xy.y - t->scrollr.min.y) / font->height;
			scroll(t, -n);
		}else if(mc->buttons == 2){
			t->offset = (mc->xy.y - t->scrollr.min.y) * t->nlines / Dy(t->scrollr);
			textdraw(t);
		}else if(mc->buttons == 4){
			n = (mc->xy.y - t->scrollr.min.y) / font->height;
			scroll(t, n);
		}
	}else if(ptinrect(mc->xy, t->textr)){
		if(mc->buttons == 0)
			selecting = 0;
		if(mc->buttons == 1){
			if(!selecting){
				selecting = 1;
				t->s0 = t->s1 = -1;
				n = indexat(t, mc->xy);
				if(n < 0)
					return;
				t->s0 = n;
				t->s1 = -1;
				textdraw(t);
			}else{
				n = indexat(t, mc->xy);
				if(n < 0)
					return;
				t->s1 = n;
			}
			for(readmouse(mc); mc->buttons == 1; readmouse(mc)){
				p = mc->xy;
				if(p.y <= t->textr.min.y){
					scroll(t, -1);
					p.y = t->textr.min.y + 1;
				}else if(p.y >= t->textr.max.y){
					scroll(t, 1);
					p.y = t->textr.max.y - 1;
				}
				n = indexat(t, p);
				if(n < 0)
					break;
				t->s1 = n;
				textdraw(t);
			}
		}else if(mc->buttons == 2){
			menu2hit(t, mc);
		}else if(mc->buttons == 8){
			n = mousescrollsize(t->vlines);
			scroll(t, -n);
		}else if(mc->buttons == 16){
			n = mousescrollsize(t->vlines);
			scroll(t, n);
		}
	}
}

