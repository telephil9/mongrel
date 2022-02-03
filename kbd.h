typedef struct Kbdctl Kbdctl;
typedef struct Key Key;

struct Kbdctl
{
	int fd;
	int pid;
	Channel *c;
};

struct Key
{
	Rune k;
	ushort mods;
};

enum
{
	Mctrl	= 1<<0,
	Malt	= 1<<1,
	Mshift	= 1<<2,
};

Kbdctl *initkbd(void);
void closekbd(Kbdctl*);
