#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include "dat.h"
#include "fns.h"
#include <ctype.h>

char	Ebadwr[]		= "bad rectangle in wctl request";
char	Ewalloc[]		= "window allocation failed in wctl request";

/* >= Top are disallowed if mouse button is pressed */
enum
{
	New,
	Resize,
	Move,
	Scroll,
	Noscroll,
	Set,
	Top,
	Bottom,
	Current,
	Hide,
	Unhide,
	Delete,
};

static char *cmds[] = {
	[New]	= "new",
	[Resize]	= "resize",
	[Move]	= "move",
	[Scroll]	= "scroll",
	[Noscroll]	= "noscroll",
	[Set]		= "set",
	[Top]	= "top",
	[Bottom]	= "bottom",
	[Current]	= "current",
	[Hide]	= "hide",
	[Unhide]	= "unhide",
	[Delete]	= "delete",
	nil
};

enum
{
	Cd,
	Deltax,
	Deltay,
	Hidden,
	Id,
	Maxx,
	Maxy,
	Minx,
	Miny,
	PID,
	R,
	Scrolling,
	Noscrolling,
};

static char *params[] = {
	[Cd]	 			= "-cd",
	[Deltax]			= "-dx",
	[Deltay]			= "-dy",
	[Hidden]			= "-hide",
	[Id]				= "-id",
	[Maxx]			= "-maxx",
	[Maxy]			= "-maxy",
	[Minx]			= "-minx",
	[Miny]			= "-miny",
	[PID]				= "-pid",
	[R]				= "-r",
	[Scrolling]			= "-scroll",
	[Noscrolling]		= "-noscroll",
	nil
};

/*
 * Check that newly created window will be of manageable size
 */
int
goodrect(Rectangle r)
{
	if(badrect(r) || !eqrect(canonrect(r), r))
		return 0;
	/* reasonable sizes only please */
	if(Dx(r) > BIG*Dx(screen->r))
		return 0;
	if(Dy(r) > BIG*Dy(screen->r))
		return 0;
	/*
	 * the height has to be big enough to fit one line of text.
	 * that includes the border on each side with an extra pixel
	 * so that the text is still drawn
	 */
	if(Dx(r) < 100 || Dy(r) < 2*(Borderwidth+1)+font->height)
		return 0;
	/* window must be on screen */
	if(!rectXrect(screen->r, r))
		return 0;
	/* must have some screen and border visible so we can move it out of the way */
	if(rectinrect(screen->r, insetrect(r, Borderwidth)))
		return 0;
	return 1;
}

static
int
word(char **sp, char *tab[])
{
	char *s, *t;
	int i;

	s = *sp;
	while(isspace(*s))
		s++;
	t = s;
	while(*s!='\0' && !isspace(*s))
		s++;
	for(i=0; tab[i]!=nil; i++)
		if(strncmp(tab[i], t, strlen(tab[i])) == 0){
			*sp = s;
			return i;
	}
	return -1;
}

int
set(int sign, int neg, int abs, int pos)
{
	if(sign < 0)
		return neg;
	if(sign > 0)
		return pos;
	return abs;
}

Rectangle
newrect(void)
{
	static int i = 0;
	int minx, miny, dx, dy;

	dx = max(Dx(screen->r) / 4, 400);
	dy = min(Dy(screen->r) / 3, Dy(screen->r) - 1.5*Borderwidth);
	minx = 16*i;
	miny = 16*i;
	i++;
	i %= 10;
	return Rect(minx, miny, minx+dx, miny+dy);
}

void
shift(int *minp, int *maxp, int min, int max)
{
	if(*maxp > max){
		*minp += max-*maxp;
		*maxp = max;
	}
	if(*minp < min){
		*maxp += min-*minp;
		if(*maxp > max)
			*maxp = max;
		*minp = min;
	}
}

Rectangle
rectonscreen(Rectangle r)
{
	shift(&r.min.x, &r.max.x, screen->r.min.x, screen->r.max.x);
	shift(&r.min.y, &r.max.y, screen->r.min.y, screen->r.max.y);
	return r;
}

/* permit square brackets, in the manner of %R */
int
riostrtol(char *s, char **t)
{
	int n;

	while(*s!='\0' && (*s==' ' || *s=='\t' || *s=='['))
		s++;
	if(*s == '[')
		s++;
	n = strtol(s, t, 10);
	if(*t != s)
		while((*t)[0] == ']')
			(*t)++;
	return n;
}


int
parsewctl(char **argp, Rectangle r, Rectangle *rp, int *pidp, int *idp, int *hiddenp, int *scrollingp, char **cdp, char *s, char *err)
{
	int cmd, n, nt, param, xy, sign;
	char *f[2], *t;

	*pidp = 0;
	*hiddenp = 0;
	*scrollingp = scrolling;
	*cdp = nil;
	cmd = word(&s, cmds);
	if(cmd < 0){
		strcpy(err, "unrecognized wctl command");
		return -1;
	}
	if(cmd == New)
		r = newrect();

	strcpy(err, "missing or bad wctl parameter");
	while((param = word(&s, params)) >= 0){
		switch(param){	/* special cases */
		case Hidden:
			*hiddenp = 1;
			continue;
		case Scrolling:
			*scrollingp = 1;
			continue;
		case Noscrolling:
			*scrollingp = 0;
			continue;
		case R:
			r.min.x = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			r.min.y = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			r.max.x = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			r.max.y = riostrtol(s, &t);
			if(t == s)
				return -1;
			s = t;
			continue;
		}
		while(isspace(*s))
			s++;
		if(param == Cd){
			*cdp = s;
			if((nt = gettokens(*cdp, f, nelem(f), " \t\r\n\v\f")) < 1)
				return -1;
			n = strlen(*cdp);
			if((*cdp)[0] == '\'' && (*cdp)[n-1] == '\'')
				((*cdp)++)[n-1] = '\0'; /* drop quotes */
			s += n+(nt-1);
			continue;
		}
		sign = 0;
		if(*s == '-'){
			sign = -1;
			s++;
		}else if(*s == '+'){
			sign = +1;
			s++;
		}
		if(!isdigit(*s))
			return -1;
		xy = riostrtol(s, &s);
		switch(param){
		case -1:
			strcpy(err, "unrecognized wctl parameter");
			return -1;
		case Minx:
			r.min.x = set(sign, r.min.x-xy, xy, r.min.x+xy);
			break;
		case Miny:
			r.min.y = set(sign, r.min.y-xy, xy, r.min.y+xy);
			break;
		case Maxx:
			r.max.x = set(sign, r.max.x-xy, xy, r.max.x+xy);
			break;
		case Maxy:
			r.max.y = set(sign, r.max.y-xy, xy, r.max.y+xy);
			break;
		case Deltax:
			r.max.x = set(sign, r.max.x-xy, r.min.x+xy, r.max.x+xy);
			break;
		case Deltay:
			r.max.y = set(sign, r.max.y-xy, r.min.y+xy, r.max.y+xy);
			break;
		case Id:
			if(idp != nil)
				*idp = xy;
			break;
		case PID:
			if(pidp != nil)
				*pidp = xy;
			break;
		}
	}

	*rp = rectonscreen(rectaddpt(r, screen->r.min));

	while(isspace(*s))
		s++;
	if(cmd!=New && *s!='\0'){
		strcpy(err, "extraneous text in wctl message");
		return -1;
	}

	if(argp)
		*argp = s;

	return cmd;
}

int
wctlnew(Rectangle rect, char *arg, int pid, int hideit, int scrollit, char *dir, char *err)
{
	char **argv;
	Image *i;

	if(!goodrect(rect)){
		strcpy(err, Ebadwr);
		return -1;
	}
	argv = emalloc(4*sizeof(char*));
	argv[0] = "rc";
	argv[1] = "-c";
	while(isspace(*arg))
		arg++;
	if(*arg == '\0'){
		argv[1] = "-i";
		argv[2] = nil;
	}else{
		argv[2] = arg;
		argv[3] = nil;
	}
	if(hideit)
		i = allocimage(display, rect, screen->chan, 0, DNofill);
	else
		i = allocwindow(wscreen, rect, Refbackup, DNofill);
	if(i == nil){
		strcpy(err, Ewalloc);
		return -1;
	}

	new(i, hideit, scrollit, pid, dir, "/bin/rc", argv);

	free(argv);	/* when new() returns, argv and args have been copied */
	return 1;
}

int
wctlcmd(Window *w, Rectangle r, int cmd, char *err)
{
	Image *i;

	switch(cmd){
	case Move:
		r = Rect(r.min.x, r.min.y, r.min.x+Dx(w->screenr), r.min.y+Dy(w->screenr));
		r = rectonscreen(r);
		/* fall through */
	case Resize:
		if(!goodrect(r)){
			strcpy(err, Ebadwr);
			return -1;
		}
		if(Dx(w->screenr) > 0){
			if(eqrect(r, w->screenr))
				return 1;
			if(w != input){
				strcpy(err, "window not current");
				return -1;
			}
			i = allocwindow(wscreen, r, Refbackup, DNofill);
		} else { /* hidden */
			if(eqrect(r, w->i->r))
				return 1;
			wuncurrent(w);
			i = allocimage(display, r, w->i->chan, 0, DNofill);
			r = ZR;
		}
		if(i == nil){
			strcpy(err, Ewalloc);
			return -1;
		}
		wsendctlmesg(w, Reshaped, r, i);
		return 1;
	case Scroll:
		w->scrolling = 1;
		wshow(w, w->nr);
		wsendctlmesg(w, Wakeup, ZR, nil);
		return 1;
	case Noscroll:
		w->scrolling = 0;
		wsendctlmesg(w, Wakeup, ZR, nil);
		return 1;
	case Top:
		wtopme(w);
		return 1;
	case Bottom:
		wbottomme(w);
		return 1;
	case Current:
		if(Dx(w->screenr)<=0){
			strcpy(err, "window is hidden");
			return -1;
		}
		wcurrent(w);
		wtopme(w);
		wsendctlmesg(w, Topped, ZR, nil);
		return 1;
	case Hide:
		switch(whide(w)){
		case -1:
			strcpy(err, "window already hidden");
			return -1;
		case 0:
			strcpy(err, "hide failed");
			return -1;
		default:
			break;
		}
		return 1;
	case Unhide:
		switch(wunhide(w)){
		case -1:
			strcpy(err, "window not hidden");
			return -1;
		case 0:
			strcpy(err, "hide failed");
			return -1;
		default:
			break;
		}
		return 1;
	case Delete:
		wsendctlmesg(w, Deleted, ZR, nil);
		return 1;
	}

	strcpy(err, "invalid wctl message");
	return -1;
}

int
writewctl(Xfid *x, char *err)
{
	int cnt, cmd, id, hideit, scrollit, pid;
	char *arg, *dir;
	Rectangle r;
	Window *w;

	w = x->f->w;
	cnt = x->count;
	x->data[cnt] = '\0';
	id = 0;

	if(w == nil)
		r = ZR;
	else
		r = rectsubpt(w->screenr, screen->r.min);
	cmd = parsewctl(&arg, r, &r, &pid, &id, &hideit, &scrollit, &dir, x->data, err);
	if(cmd < 0)
		return -1;

	if(id != 0){
		w = wlookid(id);
		if(w == 0){
			strcpy(err, "no such window id");
			return -1;
		}
	}

	if(w == nil && cmd != New){
		strcpy(err, "command needs to be run within a window");
		return -1;
	}

	switch(cmd){
	case New:
		return wctlnew(r, arg, pid, hideit, scrollit, dir, err);
	case Set:
		if(pid > 0)
			wsetpid(w, pid, 0);
		return 1;
	}

	incref(w);
	id = wctlcmd(w, r, cmd, err);
	wclose(w);

	return id;
}
