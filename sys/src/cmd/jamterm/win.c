#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include "flayer.h"
#include "samterm.h"

enum{
	SnapΔ = 24 * FLMARGIN,
};

int
max(int a, int b)
{
	return a > b ? a : b;
}

int
min(int a, int b)
{
	return a < b ? a : b;
}

Rectangle
defaultcmdrect(void)
{
	int rw, rh, fw, fh;
	Rectangle r;

	fw = stringwidth(font, "0");
	fh = font->height;
	rw = Dx(screen->r) / fw;
	rh = Dy(screen->r) / fh;
	r = screen->r;
	if(rw < 6 || rh < 3){
		;
	}else if(rw >= 120){
		r.min.x = r.max.x - 72 * fw;
		r.min.y = max(r.max.y - 8 * fh, r.min.y + 2*fh);
	}else
		r.max.y = r.min.y + 2*fh + min(max(Dy(screen->r) / 6, 0), 12*fh);
	return r;
}

/* always prefer innermost border in case of ties */
static int
cansnap(int a, int b, int Δ)
{
	int δ;

	return (δ = abs(a - b)) <= Δ && (δ != Δ || a < b);
}

/* use borders of all rects next to ours as guides to snap to */
static Rectangle
snaprect(Rectangle r, Flayer *l)
{
	int i;
	Rectangle Δr, s, rr, c;
	Flayer *fl;
	Text *t;

	rr = insetrect(r, -SnapΔ);
	Δr = Rect(SnapΔ, SnapΔ, SnapΔ, SnapΔ);
	s = r;
	for(i=0; i<nname; i++){
		t = text[i];
		if(t == nil || t->nwin == 0)
			continue;
		for(fl=t->l; fl<t->l+NL; fl++){
			if(fl->textfn == nil || fl == l)
				continue;
			c = fl->entire;
			if(!rectXrect(rr, c))
				continue;
			if(cansnap(r.min.x, c.max.x, Δr.min.x))		/* adjacent border */
				s.min.x = c.max.x, Δr.min.x = abs(r.min.x - c.max.x);
			else if(cansnap(r.min.x, c.min.x, Δr.min.x))	/* adjacent guide line */
				s.min.x = c.min.x, Δr.min.x = abs(r.min.x - c.min.x);
			if(cansnap(r.min.y, c.max.y, Δr.min.y))
				s.min.y = c.max.y, Δr.min.y = abs(r.min.y - c.max.y);
			else if(cansnap(r.min.y, c.min.y, Δr.min.y))
				s.min.y = c.min.y, Δr.min.y = abs(r.min.y - c.min.y);
			if(cansnap(c.min.x, r.max.x, Δr.max.x))
				s.max.x = c.min.x, Δr.max.x = abs(c.min.x - r.max.x);
			else if(cansnap(c.max.x, r.max.x, Δr.max.x))
				s.max.x = c.max.x, Δr.max.x = abs(c.max.x - r.max.x);
			if(cansnap(c.min.y, r.max.y, Δr.max.y))
				s.max.y = c.min.y, Δr.max.y = abs(c.min.y - r.max.y);
			else if(cansnap(c.max.y, r.max.y, Δr.max.y))
				s.max.y = c.max.y, Δr.max.y = abs(c.max.y - r.max.y);
		}
	}
	if(cansnap(r.min.x, screen->r.min.x, Δr.min.x))
		s.min.x = screen->r.min.x;
	if(cansnap(r.min.y, screen->r.min.y, Δr.min.y))
		s.min.y = screen->r.min.y;
	if(cansnap(screen->r.max.x, r.max.x, Δr.max.x))
		s.max.x = screen->r.max.x;
	if(cansnap(screen->r.max.y, r.max.y, Δr.max.y))
		s.max.y = screen->r.max.y;
	return s;
}

/* in case of overlaps, always glob the smallest rect rather than
 * the biggest to maintain splits and selectable flayers */
static Rectangle
stealrect(Point p)
{
	int i;
	Rectangle c, r;
	Flayer *fl;
	Text *t;

	r = screen->r;
	for(i=0; i<nname; i++){
		t = text[i];
		if(t == nil || t->nwin == 0)
			continue;
		for(fl=t->l; fl<t->l+NL; fl++){
			if(fl->textfn == nil
			|| !ptinrect(p, fl->entire))
				continue;
			c = fl->entire;
			if(Dx(c) * Dy(c) < Dx(r) * Dy(r))
				r = c;
		}
	}
	return r;
}

static Rectangle
fillvoid(Point p, Flayer *l, int new)
{
	int i;
	Rectangle c, r;
	Flayer *fl;
	Text *t;

	r = screen->r;
	for(i=0; i<nname; i++){
		t = text[i];
		if(t == nil || t->nwin == 0)
			continue;
		for(fl=t->l; fl<t->l+NL; fl++){
			c = fl->entire;
			if(fl->textfn == nil
			|| !rectXrect(r, c))
				continue;
			if(!new && fl == l)
				continue;
			if(ptinrect(p, fl->entire))
				return ZR;
			if(c.max.x <= p.x && c.max.x > r.min.x)
				r.min.x = c.max.x;
			if(p.x <= c.min.x && c.min.x < r.max.x)
				r.max.x = c.min.x;
			if(!rectXrect(c, r))
				continue;
			if(c.max.y <= p.y && c.max.y > r.min.y)
				r.min.y = c.max.y;
			if(p.y <= c.min.y && c.min.y < r.max.y)
				r.max.y = c.min.y;
		}
		if(Dx(r) < 2*font->width || Dy(r) < font->height)
			return ZR;
	}
	return r;
}

int
promptrect(Rectangle *rp, Flayer *l, int new)
{
	Point p;
	Rectangle r;

	r = getrect(3, mousectl);
	if(eqrect(r, ZR)){
		*rp = r;
		return 0;
	/* around the minimum for libframe to not choke */
	}else if(Dx(r) < 2 * stringwidth(font, "0") + 2 * FLMARGIN
	|| Dy(r) < font->height + 2 * FLMARGIN){
		p = r.min;	/* mousep->xy == r.max */
		if(!ptinrect(p, screen->r))
			return 0;
		r = fillvoid(p, l, new);
		if(eqrect(r, ZR))
			r = stealrect(p);
	}else
		r = snaprect(r, l);
	*rp = r;
	return 1;
}
