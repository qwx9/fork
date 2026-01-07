#include <u.h>
#include <libc.h>
#include <bio.h>
#include "midifile.h"

static void
output(void)
{
	uchar *p, eot[] = {0xff, 0x2f, 0x00};
	Track *t;

	for(t=tracks; t<tracks+ntrk; t++){
		t->Δ = 0;
		p = t->cur;
		putvar(t, t->Δ);
		put(t, eot, sizeof eot);
		t->nbuf += t->cur - p;
	}
	writemid(nil);
}

void
samp(double)
{
}

void
event(Track *t)
{
	static double τ;
	int e;
	uchar *p;
	double τ´;
	Msg msg;

	τ´ = nsec();
	if(τ == 0.0)
		τ = τ´;
	t->Δ = ns2tic(τ´ - τ);
	τ = τ´;
	p = t->cur;
	putvar(t, t->Δ);
	e = nextev(t);
	translate(t, e, &msg);
	t->nbuf += t->cur - p;
}

void
usage(void)
{
	fprint(2, "usage: %s [stream]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	writeback = 1;
	stream = 1;
	ARGBEGIN{
	default: usage();
	}ARGEND
	readmid(*argv);
	atexit(output);
	evloop();
	exits(nil);
}
