typedef struct Text Text;

enum
{
	BACK,
	BORD,
	TEXT,
	HTEXT,
	HIGH,
	NCOLS,
};

enum
{
	Maxlines = 65535,
};

struct Text
{
	Image *b;
	Rectangle r;
	Rectangle scrollr;
	Rectangle textr;
	Font *font;
	Image *cols[NCOLS];
	int vlines;
	int offset;
	char *data;
	usize ndata;
	usize lines[Maxlines];
	int nlines;
	int s0;
	int s1;
};

void textinit(Text *t, Image *b, Rectangle r, Font *f, Image *cols[NCOLS]);
void textset(Text *t, char *data, usize ndata);
void textresize(Text *t, Rectangle r);
void textkeyboard(Text *t, Rune k);
void textmouse(Text *t, Mousectl *m);
void textdraw(Text *t);

