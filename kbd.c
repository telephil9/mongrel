#include <u.h>
#include <libc.h>
#include <thread.h>
#include <keyboard.h>
#include "kbd.h"

void
kbdproc(void *v)
{
	Kbdctl *kc;
	char buf[128], buf2[128], *s;
	long n;
	Rune r;
	Key k;
	int mods = 0;

	kc = v;
	threadsetname("kbdproc");
	buf[0] = 0;
	buf2[0] = 0;
	buf2[1] = 0;
	while(kc->fd >= 0){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if (buf[0] == 0) {
			n = read(kc->fd, buf, sizeof(buf)-1);
			if (n <= 0){
				yield();
				if(kc->fd < 0)
					threadexits(nil);
				break;
			}
			buf[n-1] = 0;
			buf[n] = 0;
		}
		switch(buf[0]){
		case 'c':
			if(chartorune(&r, buf+1) > 0 && r != Runeerror){
				k = (Key){ r, mods };
				nbsend(kc->c, &k);
			}
		default:
			continue;
		case 'k':
			s = buf+1;
			while(*s){
				s += chartorune(&r, s);
				if(utfrune(buf2+1, r) == nil){
					if(r == Kctl)
						mods |= Mctrl;
					else if(r == Kalt)
						mods |= Malt;
					else if(r == Kshift)
						mods |= Mshift;
					else{
						k = (Key){r, mods};
						nbsend(kc->c, &k);
					}
				}
			}
			break;
		case 'K':
			s = buf2+1;
			while(*s){
				s += chartorune(&r, s);
				if(utfrune(buf+1, r) == nil) {
					if(r == Kctl)
						mods ^= Mctrl;
					else if(r == Kalt)
						mods ^= Malt;
					else if(r == Kshift)
						mods ^= Mshift;
				}
			}
			break;
		}
		strcpy(buf2, buf);
	}
}

Kbdctl*
initkbd(void)
{
	Kbdctl *kc;

	kc = malloc(sizeof *kc);
	if(kc == nil)
		return nil;
	kc->fd = open("/dev/kbd", OREAD);
	if(kc->fd < 0){
		free(kc);
		return nil;
	}
	kc->c = chancreate(sizeof(Key), 0);
	if(kc->c == nil){
		close(kc->fd);
		free(kc);
		return nil;
	}
	kc->pid = proccreate(kbdproc, kc, 8192);
	return kc;
}

void
closekbd(Kbdctl *kc)
{
	close(kc->fd);
	kc->fd = -1;
	threadint(kc->pid);
}

