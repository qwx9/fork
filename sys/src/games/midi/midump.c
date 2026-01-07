#include <u.h>
#include <libc.h>
#include <bio.h>
#include "midifile.h"

void
samp(double)
{
}

// FIXME: look at dumps of midis before commit and make sure they make sense
// FIXME: not *all* prints belong here, we want to be able to trace in the
//	other programs; add them back once this is complete so the picture is
//	clear

void
event(Track *t)
{
	int e;
	uchar *p;
	Msg msg;

	e = nextev(t);
	translate(t, e, &msg);

	dprint("Δ %.2f ", t->Δ);
	if(e >> 4 != 0xf)
		dprint("ch%02d ", chan - msg.c);

	// FIXME: also unhandled ones (look at the byte values; pp.43 on; tables pp.102 → array
	switch(msg.type){
	case Cnoteoff: dprint("release %02ux, aftertouch %02ux", msg.arg1, msg.arg2); break;
	case Cnoteon: dprint("trigger %02ux, velocity %02ux", msg.arg1, msg.arg2); break;
	case Cbankmsb: dprint("bank select msb %02ux", msg.arg2); break;
	case Cchanvol: dprint("channel volume %02ux", msg.chan->vol); break;
	case Cpan: dprint("stereo pan %s %02ux",
				msg.arg2 == 64 ? "center" : msg.arg2 < 64 ? "left" : "right",
				msg.arg2 == 64 ? 0 : msg.arg2 < 64 ? 64-msg.arg2 : 127-msg.arg2); break;
	case Cprogram: break;
	case Cpitchbend: break;
	case Ceot: dprint("... so long!"); break;
	case Ctempo: dprint("tempo %d", tempo); break;
	case Ckeyafter: dprint("polyphonic key pressure/aftertouch %02ux %02ux", msg.arg1, msg.arg2); break;
	case Cchanafter: dprint("channel pressure/aftertouch %02ux %02ux", msg.arg1, msg.arg2); break;
	case Csysex: break;	// FIXME
	case Csysreset:	break;	// FIXME
	case Cunknown: dprint("unhandled event %02ux, skipped", msg.type); break;
	}
	dprint(" [");
	for(p=t->run; p<t->cur; p++)
		dprint("%02ux", *p);
	dprint("]\n");
}

void
desc(void)
{
	Track *t;

	switch(mfmt){
	case 0: dprint("format 0: single multi-channel track\n"); break;
	case 1: dprint("format 1: multiple concurrent tracks\n"); break;
	case 2: dprint("format 2: multiple independent tracks\n"); break;
	}
	dprint("%d tracks\n", ntrk);
	if(1)	/* FIXME: properly introduce new members, etc. */
		dprint("div: %d ticks per quarter note\n", div);
	else
		dprint("div: %d smpte format, %d ticks per frame\n",
			(s16int)div >> 8, div & 0xff);
	if(stream)
		dprint("streaming file\n");
	else
		for(t=tracks; t<tracks+ntrk; t++)
			dprint("track %02zd: %zd bytes\n", t-tracks, t->bufsz-1);
}

void
usage(void)
{
	fprint(2, "usage: %s [-s] [mid]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	trace = 1;
	ARGBEGIN{
	case 's': stream = 1; break;
	default: usage();
	}ARGEND
	if(readmid(*argv) < 0)
		sysfatal("readmid: %r");
	desc();
	evloop();
	exits(nil);
}
