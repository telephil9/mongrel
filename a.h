typedef struct Mailbox Mailbox;
typedef struct Message Message;
typedef struct Mlist Mlist;
typedef struct Mailevent Mailevent;

struct Mailbox
{
	Lock;
	char	*name;
	char	*path;
	int		loaded;
	int		count;
	int		unseen;
	Mlist*	list;
};

struct Message
{
	int id;
	char *path;
	char *info;
	char *from;
	char *to;
	char *cc;
	char *sender;
	char *subject;
	char *date;
	long time;
	int flags;
	char *type;
	char *filename;
	char *body;
	Mlist *parts;
};

struct Mlist
{
	Message**	elts;
	usize		nelts;
	usize		size;
};

enum
{
	Fanswered	= 1<<0,
	Fdeleted	= 1<<1,
	Fdraft		= 1<<2,
	Fflagged	= 1<<3,
	Frecent		= 1<<4,
	Fseen		= 1<<5,
	Fstored		= 1<<6,
};

struct Mailevent
{
	int	type;
	char 	*path;
};

enum
{
	Enew,
	Edelete,
	Emodify,
};

Mailbox* mboxinit(char *name);
void mboxload(Mailbox*);
void mesgloadbody(Message*);
int mesgmarkseen(Mailbox*, Message*);
int mboxadd(Mailbox *mbox, char *path);
int mboxmod(Mailbox *mbox, char *path);
int mboxdel(Mailbox *mbox, char *path);
void mesgdel(Mailbox *mbox, Message *m);
void seemailproc(void *v);

Mlist* mkmlist(usize cap);
int mladd(Mlist*, Message*);
int mlins(Mlist*, usize, Message*);
Message* mldel(Mlist*, usize);

/* index */
void indexinit(Mousectl*, Channel*, Channel*, Theme*);
Rectangle indexresize(Rectangle, int);
void indexdraw(void);
void indexmouse(Mouse);
void indexkey(Rune);
void indexadded(int);
void indexremoved(int);
void indexswitch(Mailbox*);

/* pager */
void pagerinit(Mousectl*, Kbdctl*, Theme*);
void pagerresize(Rectangle);
void pagerdraw(void);
void pagermouse(Mouse);
void pagerkey(Rune);
void pagershow(Message*);

void mesgmenuhit(int, Mouse);

int kbdenter(char *ask, char *buf, int len, Mousectl *mc, Kbdctl *kc, Screen *scr);
