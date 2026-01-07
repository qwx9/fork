#include <u.h>
#include <libc.h>
#include <bio.h>
#include "commid.h"

int m2ich[Nchan], i2mch[Nchan], age[Nchan];
int nch = Nchan;
int percch = Percch;


int
mapinst(Trk *, int c, int e)
{
	int i, m, a;

	i = m2ich[c];
	if(c == 9)
		i = percch;
	else if(e >> 4 != 0x9){
		if(e >> 4 == 0x8 && i >= 0){
			i2mch[i] = -1;
			m2ich[c] = -1;
		}
		return e;
	}else if(i < 0){
		for(i=0; i<nch; i++){
			if(i == percch)
				continue;
			if(i2mch[i] < 0)
				break;
		}
		if(i == nch){
			for(m=i=a=0; i<nch; i++){
				if(i == percch)
					continue;
				if(age[i] > age[m]){
					m = i;
					a = age[i];
				}
			}
			if(a < 100){
				fprint(2, "could not remap %d\n", c);
				return e;
			}
			i = m;
			fprint(2, "remapped %d → %d\n", c, i);
		}
	}
	age[i] = 0;
	m2ich[c] = i;
	i2mch[i] = c;
	return e & ~(Nchan-1) | i;
}

int
handle(int type, int chan, int n, int m)
{
	switch(type){
	case Cnoteoff: if((e & 15) == percch && n < 36) n += 36; break;
	case Cnoteon: if((e & 15) == percch && n < 36) n += 36; break;
	case Cbankmsb: break;
	case Cchanvol: break;
	case Cpan: break;
	case Cprogram: break;
	case Cpitchbend: break;
	case Ceot: break;
	case Ctempo: tempo = n; break;
	case Ckeyafter: break;
	case Cchanafter: break;
	}
}

// FIXME: idiomatic sort of way to use midifile
static void
eat(Trk *x)
{
	int c, e;
	char *p;
	Msg m;

	p = x->p - 1;
	if((peekvar(x) & 0x80) == 0)
		*p-- = x->ev;
	e = nextevent(x);
	if(translate(x, e, &m) < 0)
		return;
	switch(m.type){
	default:
	}
	p[1] = m.e;
	p[0] = m.e >> 4 | (m.e & 0xff) << 4;
	write(1, p, x->p - p);
}


// FIXME: differenciate between midi and opl3 channels; max 18 channels,
// not 16, for a single one (also dmid); opl2: 9
//	↑ but, we're not dealing with opl here, midi only; this isn't mid2s'
//	job, we only want to forward events; filtering is for midtap (manual)
//  and dmid (opl-specific)

static void
eat(void)
{
	int c, e;
	char *p;

	p = x->p - 1;
	if((peekvar(x) & 0x80) == 0)
		*x->q-- = x->ev;
	e = nextevent(x);
	c = e & 15;
	e = mapchan(x, c, e);
	if((c & 15) == percch && n < 36)
		n += 36;
	if(translate(x, e) < 0)
		...
	x->q[1] = e;
	x->q[0] = e >> 4 | (e & 0xff) << 4;
	write(1, x->q, x->p - x->q);
	return ...
	FIXME: return condition for track end, or do in handle
}

void
main(int argc, char **argv)
{
	int i, c, end;
	Trk *x;

	ARGBEGIN{
	case 'D': trace = 1; break;
	case 'c':
		nch = atoi(EARGF(usage()));
		break;
	case 'p':
		percch = atoi(EARGF(usage()));
		break;
	default: usage();
	}ARGEND
	if(nch <= 0 || nch > Nchan
	|| percch <= 0 || percch > nch)
		usage();
	if(readmid(*argv) < 0)
		sysfatal("readmid: %r");
	for(i=0; i<nelem(m2ich); i++){
		m2ich[i] = i2mch[i] = -1;
		age[i] = -1UL;
	}
	for(;;){
		end = 1;
		for(x=tr; x<tr+ntrk; x++){
			if(x->ended)
				continue;
			end = 0;
			x->Δ--;
			while(x->Δ <= 0){
				eat(x);
				if(x->ended){
					c = x - tr;
					i = m2ich[c];
					if(i >= 0){
						i2mch[i] = -1;
						m2ich[c] = -1;
					}
					break;
				}
				x->Δ = getvar(x);
			}
		}
		if(end){
			write(1, tr[0].q, tr[0].p - tr[0].q);
			break;
		}
		samp(1);
		for(i=0; i<nch; i++){
			if(i2mch[i] < 0)
				continue;
			age[i]++;
			if(age[i] > 10000){
				fprint(2, "reset %d\n", i2mch[i]);
				m2ich[i2mch[i]] = -1;
				i2mch[i] = -1;
			}
		}
	}
	exits(nil);
}
