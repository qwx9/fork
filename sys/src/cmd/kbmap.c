#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

typedef struct KbMap KbMap;
struct KbMap {
	char *name;
	char *file;
	Rectangle r;
	int current;
};

KbMap *map;
int nmap;
Image *txt, *sel;

enum {
	PAD = 3,
	MARGIN = 5
};

char *dir = "/sys/lib/kbmap";

void*
erealloc(void *v, ulong n)
{
	v = realloc(v, n);
	if(v == nil)
		sysfatal("out of memory reallocating %lud", n);
	return v;
}

void*
emalloc(ulong n)
{
	void *v;

	v = malloc(n);
	if(v == nil)
		sysfatal("out of memory allocating %lud", n);
	memset(v, 0, n);
	return v;
}

char*
estrdup(char *s)
{
	int l;
	char *t;

	if (s == nil)
		return nil;
	l = strlen(s)+1;
	t = emalloc(l);
	memcpy(t, s, l);

	return t;
}

void
init(void)
{
	int i, fd, nr;
	Dir *pd;

	if((fd = open(dir, OREAD)) < 0)
		return;

	nmap = nr = dirreadall(fd, &pd);
	map = emalloc(nr * sizeof(KbMap));
	for(i=0; i<nr; i++){
		map[i].file = emalloc(strlen(dir) + strlen(pd[i].name) + 2);
		sprint(map[i].file, "%s/%s", dir, pd[i].name);
		map[i].name = estrdup(pd[i].name);
		map[i].current = 0;
	}
	free(pd);

	close(fd);
}

void
drawmap(int i)
{
	if(map[i].current)
		draw(screen, map[i].r, sel, nil, ZP);
	else
		draw(screen, map[i].r, display->black, nil, ZP);

	_string(screen, addpt(map[i].r.min, Pt(2,0)), txt, ZP,
		font, map[i].name, nil, strlen(map[i].name), 
		map[i].r, nil, ZP, SoverD);
	border(screen, map[i].r, 1, txt, ZP);	
}

void
geometry(void)
{
	int i, rows, cols;
	Rectangle r;

	rows = (Dy(screen->r)-2*MARGIN+PAD)/(font->height+PAD);
	if(rows < 1)
		rows = 1;
	cols = (nmap+rows-1)/rows;
	if(cols < 1)
		cols = 1;
	r = Rect(0,0,(Dx(screen->r)-2*MARGIN+PAD)/cols-PAD, font->height);
	for(i=0; i<nmap; i++)
		map[i].r = rectaddpt(rectaddpt(r, Pt(MARGIN+(PAD+Dx(r))*(i/rows),
					MARGIN+(PAD+Dy(r))*(i%rows))), screen->r.min);

}

void
redraw(Image *screen)
{
	int i;

	draw(screen, screen->r, display->black, nil, ZP);
	for(i=0; i<nmap; i++)
		drawmap(i);
	flushimage(display, 1);
}

void
eresized(int new)
{
	if(new && getwindow(display, Refmesg) < 0)
		fprint(2,"can't reattach to window");
	geometry();
	redraw(screen);
}

int
writemap(char *file)
{
	int i, fd, ofd;
	char buf[8192];
	int n;
	char *p;

	if((fd = open(file, OREAD)) < 0){
		fprint(2, "cannot open %s: %r\n", file);
		return -1;
	}
	if((ofd = open("/dev/kbmap", OWRITE|OTRUNC)) < 0){
		fprint(2, "cannot open /dev/kbmap: %r\n");
		close(fd);
		return -1;
	}
	/* do not write half lines */
	n = 0;
	while((i = read(fd, buf + n, sizeof buf - 1 - n)) > 0){
		n += i;
		buf[n] = '\0';
		p = strrchr(buf, '\n');
		if(p == nil){
			if(n == sizeof buf - 1){
				fprint(2, "writing /dev/kbmap: line too long\n");
				break;
			}
			continue;
		}
		p++;
		if(write(ofd, buf, p - buf) !=  p - buf){
			fprint(2, "writing /dev/kbmap: %r\n");
			break;
		}
		n -= p - buf;
		memmove(buf, p, n);
	}

	close(fd);
	close(ofd);
	return 0;
}

void
click(Mouse m)
{
	int i, j;

	if(m.buttons == 0 || (m.buttons & ~4))
		return;

	for(i=0; i<nmap; i++)
		if(ptinrect(m.xy, map[i].r))
			break;
	if(i == nmap)
		return;

	do
		m = emouse();
	while(m.buttons == 4);

	if(m.buttons != 0){
		do
			m = emouse();
		while(m.buttons);
		return;
	}

	for(j=0; j<nmap; j++)
		if(ptinrect(m.xy, map[j].r))
			break;
	if(j != i)
		return;

	writemap(map[i].file);

	/* clean the previous current map */
	for(j=0; j<nmap; j++)
		map[j].current = 0;

	map[i].current = 1;

	redraw(screen);
}

void
usage(void)
{
	fprint(2, "usage: kbmap [file...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Event e;
	char *c;

	if(argc > 1) {
		argv++; argc--;
		map = emalloc((argc)*sizeof(KbMap));
		while(argc--) {
			map[argc].file = estrdup(argv[argc]);
			c = strrchr(map[argc].file, '/');
			map[argc].name = (c == nil ? map[argc].file : c+1);
			map[argc].current = 0;
			nmap++;
		}
	} else 
		init();

	if(initdraw(0, 0, "kbmap") < 0){
		fprint(2, "kbmap: initdraw failed: %r\n");
		exits("initdraw");
	}
	enum{
		Ctxt,
		Csel,
		Ncols,
	};
	Theme th[Ncols] = {
		[Ctxt] { "text",	0xEAFFFFFF },
		[Csel] { "hold", 	DBlue },
	};
	readtheme(th, nelem(th), nil);
	txt = allocimage(display, Rect(0,0,1,1), screen->chan, 1, th[Ctxt].c);
	sel = allocimage(display, Rect(0,0,1,1), screen->chan, 1, th[Csel].c);
	if(txt == nil || sel == nil)
		sysfatal("allocimage: %r");

	eresized(0);
	einit(Emouse|Ekeyboard);

	for(;;){
		switch(eread(Emouse|Ekeyboard, &e)){
		case Ekeyboard:
			if(e.kbdc==Kdel || e.kbdc=='q')
				exits(0);
			break;
		case Emouse:
			if(e.mouse.buttons)
				click(e.mouse);
			break;
		}
	}
}

