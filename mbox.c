#include <u.h>
#include <libc.h>
#include <bio.h>
#include <plumb.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include "theme.h"
#include "a.h"

char*
slurp(char *path)
{
	int fd;
	long r, n, s;
	char *buf;

	n = 0;
	s = 8192;
	buf = malloc(s);
	if(buf == nil)
		return nil;
	fd = open(path, OREAD);
	if(fd < 0)
		return nil;
	for(;;){
		r = read(fd, buf + n, s - n);
		if(r < 0)
			return nil;
		if(r == 0)
			break;
		n += r;
		if(n == s){
			s *= 1.5;
			buf = realloc(buf, s);
			if(buf == nil)
				return nil;
		}
	}
	buf[n] = 0;
	close(fd);
	return buf;
}


char*
readpart(char *path, char *part)
{
	Biobuf *bp;
	char *f, *s;

	f = smprint("%s/%s", path, part);
	if(f==nil)
		sysfatal("smprint: %r");
	bp = Bopen(f, OREAD);
	if(bp==nil)
		sysfatal("bopen %s: %r", f);
	s = Brdstr(bp, '\n', 1);
	Bterm(bp);
	return s;
}

int 
parseflags(char *s)
{
	int f;

	f = 0;
	if(s[0]!='-')
		f |= Fanswered;
	if(s[5]!='-')
		f |= Fseen;
	return f;
}

int
readflags(char *path)
{
	char *f;
	int i;

	f = readpart(path, "flags");
	i = parseflags(f);
	free(f);
	return i;
}

#define Datefmt		"?WWW, ?MMM ?DD hh:mm:ss ?Z YYYY"

/* most code stolen from nedmail */
Message*
loadmessage(char *path)
{
	Message *m;
	char p[256], *f[20+1];
	int n;
	Tm tm;

	snprint(p, sizeof p, "%s/info", path);
	m = mallocz(sizeof *m, 1);
	if(m==nil)
		sysfatal("mallocz: %r");
	m->path = strdup(path);
	m->info = slurp(p);
	if(m->info == nil)
		sysfatal("read info: %r");
	n = getfields(m->info, f, nelem(f), 0, "\n");
	if(n < 17)
		sysfatal("invalid info file %s: only %d fields", path, n);
	m->from = f[0];
	m->to = f[1];
	m->cc = f[2];
	m->date = f[4];
	m->subject = f[5];
	m->type = f[6];
	m->filename = f[8];
	if(n > 17)
		m->flags = parseflags(f[17]);
	else
		m->flags = readflags(path);
	if(n >= 20 && f[19] != nil && strlen(f[19]) > 0)
		m->sender = strdup(f[19]);
	else
		m->sender = strdup(m->from);
	m->time = time(nil);
	if(tmparse(&tm, Datefmt, m->date, nil, nil) != nil)
		m->time = tmnorm(&tm);
	return m;
}

void
mesgloadbody(Message *m)
{
	char path[255];
	int i;
	Dir *d;
	Message *p;

	if(m->body != nil || m->parts != nil)
		return;
	snprint(path, sizeof path, "%s/body", m->path);
	m->body = slurp(path);
	if(m->type && strncmp(m->type, "multipart/", 10) == 0){
		m->parts = mkmlist(8);
		for(i = 1; ; i++){
			snprint(path, sizeof path, "%s/%d", m->path, i);
			d = dirstat(path);
			if(d == nil)
				break;
			if((d->qid.type & QTDIR) != QTDIR){
				free(d);
				break;
			}
			free(d);
			p = loadmessage(path);
			mesgloadbody(p);
			mladd(m->parts, p);
		}
	}
}

int
mesgmarkseen(Mailbox *mbox, Message *m)
{
	char path[255];
	int fd;

	if(m->flags & Fseen)
		return 0;
	snprint(path, sizeof path, "%s/flags", m->path);
	fd = open(path, OWRITE);
	if(fd < 0)
		return 0;
	fprint(fd, "+s");
	close(fd);
	m->flags |= Fseen;
	mbox->unseen -= 1;
	return 1;
}

int
dircmp(Dir *a, Dir *b)
{
	return atoi(a->name) - atoi(b->name);
}

Mailbox*
loadmbox(char *name)
{
	Mailbox *mb;
	Dir *d;
	int n, fd, i;
	char buf[256];
	Message *m;

	mb = mallocz(sizeof(Mailbox), 1);
	if(mb==nil)
		sysfatal("malloc: %r");
	mb->name = strdup(name);
	mb->path = smprint("/mail/fs/%s", name);
	fd = open(mb->path, OREAD);
	if(fd<0)
		sysfatal("open: %r");
	n = dirreadall(fd, &d);
	close(fd);
	qsort(d, n, sizeof *d, (int(*)(void*,void*))dircmp);
	mb->list = mkmlist(n*1.5);
	for(i = 1; i < n; i++){
		snprint(buf, sizeof buf, "%s/%s", mb->path, d[i].name);
		if((d[i].qid.type & QTDIR)==0)
			continue;
		m = loadmessage(buf);
		m->id = atoi(d[i].name);
		mladd(mb->list, m);
		if((m->flags & Fseen) == 0)
			++mb->unseen;
		++mb->count;
	}
	free(d);
	return mb;
}

int
mboxadd(Mailbox *mbox, char *path)
{
	Message *m;
	int i, id;

	id = atoi(path+strlen(mbox->path)+1);
	for(i = 0; i < mbox->list->nelts; i++){
		m = mbox->list->elts[i];
		if(m->id == id)
			return -1;
	}
	m = loadmessage(path);
	m->id = id;
	if((m->flags & Fseen) == 0)
		++mbox->unseen;
	mladd(mbox->list, m);
	++mbox->count;
	return mbox->count - 1;
}

int
mboxmod(Mailbox *mbox, char *path)
{
	Message *m;
	int i, f;

	m = nil;
	for(i = 0; i < mbox->list->nelts; i++){
		m = mbox->list->elts[i];
		if(strcmp(path, m->path)==0)
			break;
	}
	if(m==nil){
		fprint(2, "could not find mail '%s'\n", path);
		return -1;
	}
	f = readflags(path);
	if(m->flags != f){
		if((m->flags & Fseen) != 0 && (f & Fseen) == 0)
			++mbox->unseen;
		else if((m->flags & Fseen) == 0 && (f & Fseen) != 0)
			--mbox->unseen;
		m->flags = f;
		return i;
	}
	return -1;
}

int
mboxdel(Mailbox *mbox, char *path)
{
	Message *m;
	int i;

	m = nil;
	for(i = 0; i < mbox->list->nelts; i++){
		m = mbox->list->elts[i];
		if(strcmp(path, m->path)==0){
			if((m->flags & Fseen) == 0)
				--mbox->unseen;
			mldel(mbox->list, i);
			free(m->path);
			free(m->info);
			free(m->sender);
			free(m);
			--mbox->count;
			return i;
		}
	}
	if(m==nil){
		fprint(2, "could not find mail '%s'\n", path);
	}
	return -1;
}

void
mesgdel(Mailbox *mbox, Message *m)
{
	int fd;

	fd = open("/mail/fs/ctl", OWRITE);
	if(fd<0)
		sysfatal("open: %r");
	fprint(fd, "delete %s %s", mbox->name, m->path+strlen(mbox->path)+1);
	close(fd);
}

void
seemailproc(void *v)
{
	Channel *c;
	Mailevent e;
	Plumbmsg *m;
	int fd;
	char *s;

	c = v;
	threadsetname("seemailproc");
	fd = plumbopen("seemail", OREAD);
	if(fd<0)
		sysfatal("cannot open seemail: %r");
	for(;;){
		m = plumbrecv(fd);
		if(m==nil)
			sysfatal("seemail plumbrecv: %r");
		s = plumblookup(m->attr, "filetype");
		if(s != nil && strcmp(s, "vwhois") == 0){
			plumbfree(m);
			continue;
		}
		s = plumblookup(m->attr, "mailtype");
		if(strcmp(s, "modify")==0)
			e.type = Emodify;
		else if(strcmp(s, "delete")==0)
			e.type = Edelete;
		else if(strcmp(s, "new")==0)
			e.type = Enew;
		else
			fprint(2, "received unknown message type: %s\n", s);
		e.path = strdup(m->data);
		send(c, &e);
		plumbfree(m);
	}
}
