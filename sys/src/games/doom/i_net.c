// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include <bio.h>
#include <ndb.h>
#include <ip.h>
#include <thread.h>
#include "m_argv.h"
#include "i_system.h"
#include "d_net.h"
#include "i_net.h"
#include "w_wad.h"

typedef struct Addr Addr;

enum{
	HDRSZ = 16+16+16+2+2	/* sizeof Udphdr w/o padding */
};

static char lsrv[6] = "666";

struct Addr{
	Udphdr h;
	char srv[6];	/* convenience */
	int ready;	/* is connected to udp!*!lsrv */
	long called;
};
static Addr raddr[MAXNETNODES];

static int ucfd;
static int udfd;
static int upfd[2];
static int upid;


static void
conreq(doomdata_t *d)
{
	int fd;
	long t;
	char ip[64];
	Addr *p;

	p = &raddr[doomcom->remotenode];

	t = time(nil);
	if(t - p->called < 1)
		return;

	snprint(ip, sizeof ip, "%I", p->h.raddr);
	if((fd = dial(netmkaddr(ip, "udp", p->srv), lsrv, nil, nil)) < 0)
		sysfatal("dial: %r");
	if(write(fd, d, doomcom->datalength) != doomcom->datalength)
		sysfatal("conreq: %r");
	close(fd);
	p->called = t;
}

static void
dsend(void)
{
	int i;
	uchar buf[HDRSZ+sizeof(doomdata_t)];
	doomdata_t d;

	hnputl(&d.checksum, netbuffer->checksum);
	d.player = netbuffer->player;
	d.retransmitfrom = netbuffer->retransmitfrom;
	d.starttic = netbuffer->starttic;
	d.numtics = netbuffer->numtics;

	for(i = 0; i < netbuffer->numtics; i++){
		d.cmds[i].forwardmove = netbuffer->cmds[i].forwardmove;
		d.cmds[i].sidemove = netbuffer->cmds[i].sidemove;
		hnputs(&d.cmds[i].angleturn, netbuffer->cmds[i].angleturn);
		hnputs(&d.cmds[i].consistancy, netbuffer->cmds[i].consistancy);
		d.cmds[i].chatchar = netbuffer->cmds[i].chatchar;
		d.cmds[i].buttons = netbuffer->cmds[i].buttons;
	}

	if(!raddr[doomcom->remotenode].ready){
		conreq(&d);
		return;
	}
	memcpy(buf, &raddr[doomcom->remotenode].h, HDRSZ);
	memcpy(buf+HDRSZ, &d, sizeof d);

	i = doomcom->datalength + HDRSZ;
	if(write(udfd, buf, i) != i)
		sysfatal("dsend: %r");
}

static void
drecv(void)
{
	int n;
	ushort i;
	doomdata_t d;

	if(filelength(upfd[1]) < 1){
		doomcom->remotenode = -1;
		return;
	}
	if((n = read(upfd[1], &d, sizeof d)) <= 0
	|| read(upfd[1], &i, sizeof i) <= 0)
		sysfatal("drecv: %r");

	doomcom->remotenode = i;
	doomcom->datalength = n;

	/* FIXME: proper read/write from/to struct */
	netbuffer->checksum = nhgetl(&d.checksum);
	netbuffer->player = d.player;
	netbuffer->retransmitfrom = d.retransmitfrom;
	netbuffer->starttic = d.starttic;
	netbuffer->numtics = d.numtics;
	for(i = 0; i < netbuffer->numtics; i++){
		netbuffer->cmds[i].forwardmove = d.cmds[i].forwardmove;
		netbuffer->cmds[i].sidemove = d.cmds[i].sidemove;
		netbuffer->cmds[i].angleturn = nhgets(&d.cmds[i].angleturn);
		netbuffer->cmds[i].consistancy = nhgets(&d.cmds[i].consistancy);
		netbuffer->cmds[i].chatchar = d.cmds[i].chatchar;
		netbuffer->cmds[i].buttons = d.cmds[i].buttons;
	}
}

static void
uproc(void*)
{
	int n;
	ushort i;
	uchar buf[HDRSZ+sizeof(doomdata_t)];
	Udphdr h;

	upid = getpid();
	for(;;){
		if((n = read(udfd, buf, sizeof buf)) <= 0)
			break;
		memcpy(&h, buf, HDRSZ);

		for(i = 0; i < doomcom->numnodes; i++)
			if(equivip6(h.raddr, raddr[i].h.raddr)
			&& nhgets(h.rport) == nhgets(raddr[i].h.rport))
				break;
		if(i == doomcom->numnodes)
			continue;	/* ignore messages from strangers */
		if(!raddr[i].ready){	/* FIXME: urgh */
			raddr[i].ready++;
			memcpy(&raddr[i].h, &h, sizeof h);
		}

		if(write(upfd[0], buf+HDRSZ, n - HDRSZ) != n - HDRSZ
		|| write(upfd[0], &i, sizeof i) != sizeof i)
			break;
	}
}

void
I_NetCmd(void)
{
	if(doomcom->command == CMD_SEND)
		dsend();
	else if(doomcom->command == CMD_GET)
		drecv();
	else
		I_Error("invalid netcmd %d", doomcom->command);
}

void
I_ShutdownNet(void)
{
	postnote(PNPROC, upid, "shutdown");
	close(upfd[0]);
	close(upfd[1]);
	close(udfd);
	close(ucfd);
}

static void
initudp(void)
{
	char data[64], adir[40];

	/* FIXME */
	//if(myipaddr(raddr[0].h.raddr, nil) < 0)
	//	sysfatal("myipaddr: %r");

	if((ucfd = announce(netmkaddr("*", "udp", lsrv), adir)) < 0)
		sysfatal("announce: %r");
	if(fprint(ucfd, "headers") < 0)
		sysfatal("failed to set headers mode: %r");
	snprint(data, sizeof data, "%s/data", adir);
	if((udfd = open(data, ORDWR)) < 0)
		sysfatal("open: %r");

	if(pipe(upfd) < 0)
		sysfatal("pipe: %r");
	if(procrfork(uproc, nil, mainstacksize, RFFDG) < 0)
		sysfatal("procrfork: %r");
}

static void
csip(char *s, Addr *a)	/* raddr!rsrv */
{
	int fd, n;
	char buf[128], *f[3];

	/* FIXME: get netmnt... */

	if((fd = open("/net/cs", ORDWR)) < 0)
		sysfatal("open: %r");

	snprint(buf, sizeof buf, "udp!%s", s);
	n = strlen(buf);
	if(write(fd, buf, n) != n)
		sysfatal("translating %s: %r", s);

	seek(fd, 0, 0);
	if((n = read(fd, buf, sizeof(buf)-1)) <= 0)
		sysfatal("reading cs tables: %r");
	buf[n] = 0;
	close(fd);

	if(getfields(buf, f, 3, 1, " !") < 2)
		sysfatal("bad cs entry %s", buf);

	if(parseip(a->h.raddr, f[1]) < 0)
		sysfatal("parseip: %r");
	hnputs(a->h.rport, atoi(f[2]));	/* FIXME */
	strncpy(a->srv, f[2], sizeof(a->srv)-1);
}

static int
netopts(void)
{
	int i;

	if((i = M_CheckParm("-dup")) && i < myargc - 1){
		doomcom->ticdup = myargv[i+1][0] - '0';
		if(doomcom->ticdup < 1)
			doomcom->ticdup = 1;
		if(doomcom->ticdup > 9)
			doomcom->ticdup = 9;
	}

	if(M_CheckParm("-extratic"))
		doomcom->extratics = 1;

	if((i = M_CheckParm("-srv")) && i < myargc - 1)
		strncpy(lsrv, myargv[i+1], sizeof(lsrv)-1);

	/* [0-3], default 0; player 0 is special */
	if((i = M_CheckParm("-pn")) && i < myargc - 1)
		doomcom->consoleplayer = myargv[i+1][0] - '0';

	/* FIXME: d_net.c: don't use remoteaddr=0 as special case (max+1?) */
	/* remote host address list: -net raddr!rsrv.. */
	if((i = M_CheckParm("-net")) == 0){
		/* single player game */
		doomcom->id = DOOMCOM_ID;
		doomcom->numplayers = doomcom->numnodes = 1;
		doomcom->deathmatch = false;
		netgame = false;
		return -1;
	}
	doomcom->numnodes++;	/* raddr[0] is special cased because ??? */
	while(++i < myargc && myargv[i][0] != '-'){
		csip(myargv[i], &raddr[doomcom->numnodes]);
		doomcom->numnodes++;
	}

	return 0;
}

void
I_InitNetwork(void)
{
	doomcom = malloc(sizeof *doomcom);
	memset(doomcom, 0, sizeof *doomcom);

	doomcom->ticdup = 1;
	doomcom->extratics = 0;
	if(netopts() < 0)
		return;
	if(doomcom->numnodes < 2)
		I_Error("netgame with a single node");
	doomcom->id = DOOMCOM_ID;
	doomcom->numplayers = doomcom->numnodes;

	fmtinstall('I', eipfmt);
	initudp();

	netgame = true;
}
