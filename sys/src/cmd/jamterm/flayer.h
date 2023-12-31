typedef enum Vis{
	None=0,
	Some,
	All,
}Vis;

enum{
	Clicktime=500,		/* milliseconds */
};

typedef struct Flayer Flayer;

struct Flayer
{
	Frame		f;
	long		origin;	/* offset of first char in flayer */
	long		p0, p1;
	long		click;	/* time at which selection click occurred, in HZ */
	Point		warpto;
	Rune		*(*textfn)(Flayer*, long, ulong*);
	int		user0;
	void		*user1;
	Rectangle	entire;
	Rectangle	scroll;
	Rectangle	lastsr;	/* geometry of scrollbar when last drawn */
	Vis		visible;

	Flayer *lprev;
	Flayer *lnext;
};

void	flborder(Flayer*, int);
void	flclose(Flayer*);
void	fldelete(Flayer*, long, long);
void	flfp0p1(Flayer*, ulong*, ulong*);
void	flinit(Flayer*, Rectangle, Font*, Image**);
void	flinsert(Flayer*, Rune*, Rune*, long);
void	flnew(Flayer*, Rune *(*fn)(Flayer*, long, ulong*), int, void*);
int	flprepare(Flayer*);
Rectangle flrect(Flayer*, Rectangle);
void	flrefresh(Flayer*, Rectangle, int);
void	flresize(Rectangle);
int	flselect(Flayer*, ulong*);
void	flsetselect(Flayer*, long, long);
void	flstart(Rectangle);
void	flupfront(Flayer*);
Flayer	*flwhich(Point);

#define	FLMARGIN	2
#define	FLSCROLLWID	18
#define	FLGAP		0

extern	Image	*maincols[NCOL];
extern	Image	*cmdcols[NCOL];
