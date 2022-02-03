#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include "theme.h"
#include "a.h"

void*
emalloc(ulong size)
{
	void *p;

	p = malloc(size);
	if(p == nil)
		sysfatal("malloc: %r");
	return p;
}

void*
erealloc(void *p, ulong size)
{
	p = realloc(p, size);
	if(p == nil)
		sysfatal("realloc: %r");
	return p;
}

void
mlmaybegrow(Mlist *ml)
{
	if(ml->nelts < ml->size)
		return;
	ml->size *= 1.5;
	ml->elts = erealloc(ml->elts, ml->size * sizeof(Message*));
}

Mlist*
mkmlist(usize cap)
{
	Mlist *ml;

	ml = emalloc(sizeof *ml);
	ml->size = cap;
	ml->nelts = 0;
	ml->elts = emalloc(cap * sizeof(Message*));
	return ml;
}

int
mladd(Mlist *ml, Message *m)
{
	mlmaybegrow(ml);
	ml->elts[ml->nelts] = m;
	ml->nelts += 1;
	return 0;
}

int
mlinsert(Mlist *ml, usize index, Message *m)
{
	if(index > ml->nelts || index >= ml->size)
		return -1;
	mlmaybegrow(ml);
	memmove(&ml->elts[index+1], &ml->elts[index], (ml->nelts - index + 1)*sizeof(Message*));
	ml->elts[index] = m;
	ml->nelts += 1;
	return 0;
}

Message*
mldel(Mlist *ml, usize index)
{
	Message *m;

	if(index >= ml->nelts || index >= ml->size)
		return nil;
	m = ml->elts[index];
	memmove(&ml->elts[index], &ml->elts[index+1], (ml->nelts -index)*sizeof(Message*));
	ml->nelts -= 1;
	return m;
}

