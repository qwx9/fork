/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include "awk.h"
#include "y.tab.h"

extern YYSTYPE	yylval;
extern int	infunc;

int	lineno	= 1;
int	bracecnt = 0;
int	brackcnt  = 0;
int	parencnt = 0;

typedef struct Keyword {
	char	*word;
	int	sub;
	int	type;
} Keyword;

Keyword keywords[] ={	/* keep sorted: binary searched */
	{ "BEGIN",	XBEGIN,		XBEGIN },
	{ "END",	XEND,		XEND },
	{ "NF",		VARNF,		VARNF },
	{ "atan2",	FATAN,		BLTIN },
	{ "break",	BREAK,		BREAK },
	{ "close",	CLOSE,		CLOSE },
	{ "continue",	CONTINUE,	CONTINUE },
	{ "cos",	FCOS,		BLTIN },
	{ "delete",	DELETE,		DELETE },
	{ "do",		DO,		DO },
	{ "else",	ELSE,		ELSE },
	{ "exit",	EXIT,		EXIT },
	{ "exp",	FEXP,		BLTIN },
	{ "fflush",	FFLUSH,		BLTIN },
	{ "for",	FOR,		FOR },
	{ "func",	FUNC,		FUNC },
	{ "function",	FUNC,		FUNC },
	{ "getline",	GETLINE,	GETLINE },
	{ "gsub",	GSUB,		GSUB },
	{ "if",		IF,		IF },
	{ "in",		IN,		IN },
	{ "index",	INDEX,		INDEX },
	{ "int",	FINT,		BLTIN },
	{ "length",	FLENGTH,	BLTIN },
	{ "log",	FLOG,		BLTIN },
	{ "match",	MATCHFCN,	MATCHFCN },
	{ "next",	NEXT,		NEXT },
	{ "nextfile",	NEXTFILE,	NEXTFILE },
	{ "print",	PRINT,		PRINT },
	{ "printf",	PRINTF,		PRINTF },
	{ "rand",	FRAND,		BLTIN },
	{ "return",	RETURN,		RETURN },
	{ "sin",	FSIN,		BLTIN },
	{ "split",	SPLIT,		SPLIT },
	{ "sprintf",	SPRINTF,	SPRINTF },
	{ "sqrt",	FSQRT,		BLTIN },
	{ "srand",	FSRAND,		BLTIN },
	{ "sub",	SUB,		SUB },
	{ "substr",	SUBSTR,		SUBSTR },
	{ "system",	FSYSTEM,	BLTIN },
	{ "tolower",	FTOLOWER,	BLTIN },
	{ "toupper",	FTOUPPER,	BLTIN },
	{ "utf",	FUTF,		BLTIN },
	{ "while",	WHILE,		WHILE },
};

#ifdef	DEBUG
#define	RET(x)	{ if(dbg)print("lex %s\n", tokname(x)); return(x); }
#else
#define	RET(x)	return(x)
#endif

Rune peek(void)
{
	Rune c = input();
	unput(c);
	return c;
}

static int gettok(char **pbuf, int *psz, Awkfloat *fp)	/* get next input token */
{
	Rune c;
	char *buf = *pbuf;
	int sz = *psz;
	char *bp = buf;

	c = input();
	if (c == 0)
		return 0;
	bp += runetochar(bp, &c);
	*bp = 0;

	if (c < Runeself && !isalnum(c) && c != L'.' && c != L'_')
		return c;

	if (c >= Runeself || isalpha(c) || c == L'_') {	/* it's a varname */
		for ( ; (c = input()) != 0; ) {
			if (bp-buf >= sz)
				if (!adjbuf(&buf, &sz, bp-buf+UTFmax+1, 100, &bp, 0))
					FATAL( "out of space for name %.10s...", buf );
			if (c >= Runeself || isalnum(c) || c == L'_')
				bp += runetochar(bp, &c);
			else {
				*bp = 0;
				unput(c);
				break;
			}
		}
		c = L'a';
	} else {	/* it's a number */
		char *rem;
		/* read input until can't be a number */
		for ( ; (c = input()) != 0; ) {
			if (bp-buf >= sz)
				if (!adjbuf(&buf, &sz, bp-buf+UTFmax+1, 100, &bp, 0))
					FATAL( "out of space for number %.10s...", buf );
			if (isdigit(c) || c == L'e' || c == L'E' 
			  || c == L'.' || c == L'+' || c == L'-')
				*bp++ = c;
			else {
				unput(c);
				break;
			}
		}
		*bp = 0;
		if(to_number(buf, fp, &rem))	/* parse the number */
			c = L'0';
		else
			c = buf[0];
		unputstr(rem);		/* put rest back for later */
		rem[0] = 0;
	}
	*pbuf = buf;
	*psz = sz;
	return c;
}

int	word(char *);
int	string(void);
int	regexpr(void);
int	sc	= 0;	/* 1 => return a } right now */
int	reg	= 0;	/* 1 => return a REGEXPR now */

int yylex(void)
{
	Rune c;
	Awkfloat f;
	static char *buf = 0;
	static int bufsize = 500;

	if (buf == 0 && (buf = (char *) malloc(bufsize)) == nil)
		FATAL( "out of space in yylex" );
	if (sc) {
		sc = 0;
		RET(L'}');
	}
	if (reg) {
		reg = 0;
		return regexpr();
	}
	for (;;) {
		c = gettok(&buf, &bufsize, &f);
		if (c == 0)
			return 0;
		if (c == L'a')
			return word(buf);
		/* may be unsuitable for printing (T.strnum) so don't set STR,
		 * but may be a regex to be treated literally (T.coerce[23])
		 * via strnode, so save a copy. */
		if (c == L'0') {
			yylval.cp = setsymtab(buf, buf, f, CON|NUM|FCONV, symtab);
			RET(NUMBER);
		}
	
		yylval.i = c;
		switch (c) {
		case L'\n':	/* {EOL} */
			RET(NL);
		case L'\r':	/* assume \n is coming */
		case L' ':	/* {WS}+ */
		case L'\t':
			break;
		case L'#':	/* #.* strip comments */
			while ((c = input()) != L'\n' && c != 0)
				;
			unput(c);
			break;
		case L';':
			RET(L';');
		case L'\\':
			if (peek() == L'\n') {
				input();
			} else if (peek() == L'\r') {
				input(); input();	/* \n */
				lineno++;
			} else {
				RET(c);
			}
			break;
		case L'&':
			if (peek() == L'&') {
				input(); RET(AND);
			} else 
				RET(L'&');
		case L'|':
			if (peek() == L'|') {
				input(); RET(BOR);
			} else
				RET(L'|');
		case L'!':
			if (peek() == L'=') {
				input(); yylval.i = NE; RET(NE);
			} else if (peek() == L'~') {
				input(); yylval.i = NOTMATCH; RET(MATCHOP);
			} else
				RET(NOT);
		case L'~':
			yylval.i = MATCH;
			RET(MATCHOP);
		case L'<':
			if (peek() == L'=') {
				input(); yylval.i = LE; RET(LE);
			} else {
				yylval.i = LT; RET(LT);
			}
		case L'=':
			if (peek() == L'=') {
				input(); yylval.i = EQ; RET(EQ);
			} else {
				yylval.i = ASSIGN; RET(ASGNOP);
			}
		case L'>':
			if (peek() == L'=') {
				input(); yylval.i = GE; RET(GE);
			} else if (peek() == L'>') {
				input(); yylval.i = APPEND; RET(APPEND);
			} else {
				yylval.i = GT; RET(GT);
			}
		case L'+':
			if (peek() == L'+') {
				input(); yylval.i = INCR; RET(INCR);
			} else if (peek() == L'=') {
				input(); yylval.i = ADDEQ; RET(ASGNOP);
			} else
				RET(L'+');
		case L'-':
			if (peek() == L'-') {
				input(); yylval.i = DECR; RET(DECR);
			} else if (peek() == L'=') {
				input(); yylval.i = SUBEQ; RET(ASGNOP);
			} else
				RET(L'-');
		case L'*':
			if (peek() == L'=') {	/* *= */
				input(); yylval.i = MULTEQ; RET(ASGNOP);
			} else if (peek() == L'*') {	/* ** or **= */
				input();	/* eat 2nd * */
				if (peek() == L'=') {
					input(); yylval.i = POWEQ; RET(ASGNOP);
				} else {
					RET(POWER);
				}
			} else
				RET(L'*');
		case L'/':
			RET(L'/');
		case L'%':
			if (peek() == L'=') {
				input(); yylval.i = MODEQ; RET(ASGNOP);
			} else
				RET(L'%');
		case L'^':
			if (peek() == L'=') {
				input(); yylval.i = POWEQ; RET(ASGNOP);
			} else
				RET(POWER);
	
		case L'$':
			/* BUG: awkward, if not wrong */
			c = gettok(&buf, &bufsize, &f);
			if (c == L'(' || c == L'[' || (infunc && isarg(buf) >= 0)) {
				unputstr(buf);
				RET(INDIRECT);
			} else if (isalpha(c)) {
				if (strcmp(buf, "NF") == 0) {	/* very special */
					unputstr("(NF)");
					RET(INDIRECT);
				}
				yylval.cp = setsymtab(buf, EMPTY, 0.0, STR|NUM, symtab);
				RET(IVAR);
			} else {
				unputstr(buf);
				RET(INDIRECT);
			}
	
		case L'}':
			if (--bracecnt < 0)
				SYNTAX( "extra }" );
			sc = 1;
			RET(L';');
		case L']':
			if (--brackcnt < 0)
				SYNTAX( "extra ]" );
			RET(L']');
		case L')':
			if (--parencnt < 0)
				SYNTAX( "extra )" );
			RET(L')');
		case L'{':
			bracecnt++;
			RET(L'{');
		case L'[':
			brackcnt++;
			RET(L'[');
		case L'(':
			parencnt++;
			RET(L'(');
	
		case L'"':
			return string();	/* BUG: should be like tran.c ? */
	
		default:
			RET(c);
		}
	}
}

int string(void)
{
	int n;
	Rune c;
	char *s, *bp;
	static char *buf = 0;
	static int bufsz = 500;

	if (buf == 0 && (buf = (char *) malloc(bufsz)) == nil)
		FATAL("out of space for strings");
	for (bp = buf; (c = input()) != L'"'; ) {
		if (!adjbuf(&buf, &bufsz, bp-buf+UTFmax+1, 500, &bp, 0)){
			*bp = 0;
			FATAL("out of space for string %.10s...", buf);
		}
		switch (c) {
		case L'\n':
		case L'\r':
		case 0:		
			*bp = 0;
			SYNTAX( "non-terminated string %.10s...", buf );
			lineno++;
			RET(0);
		case L'\\':
			c = input();
			switch (c) {
			case L'"': *bp++ = L'"'; break;
			case L'n': *bp++ = L'\n'; break;	
			case L't': *bp++ = L'\t'; break;
			case L'f': *bp++ = L'\f'; break;
			case L'r': *bp++ = L'\r'; break;
			case L'b': *bp++ = L'\b'; break;
			case L'v': *bp++ = L'\v'; break;
			case L'a': *bp++ = '\007'; break;
			case L'\\': *bp++ = L'\\'; break;

			case L'0': case L'1': case L'2': /* octal: \d \dd \ddd */
			case L'3': case L'4': case L'5': case L'6': case L'7':
				n = c - L'0';
				if ((c = peek()) >= L'0' && c < L'8') {
					n = 8 * n + input() - L'0';
					if ((c = peek()) >= L'0' && c < L'8')
						n = 8 * n + input() - L'0';
				}
				c = n;
				bp += runetochar(bp, &c);
				break;

			case L'x':	/* hex  \x0-9a-fA-F + */
			    {	char xbuf[100], *px;
				for (px = xbuf; (c = input()) != 0 && px-xbuf < 100-2; ) {
					if (isdigit(c)
					 || (c >= L'a' && c <= L'f')
					 || (c >= L'A' && c <= L'F'))
						*px++ = c;
					else
						break;
				}
				*px = 0;
				unput(c);
				c = strtol(xbuf, nil, 16);
				bp += runetochar(bp, &c);
				break;
			    }

			default: 
				bp += runetochar(bp, &c);
				break;
			}
			break;
		default:
			bp += runetochar(bp, &c);
			break;
		}
	}
	*bp = 0; 
	s = tostring(buf);
	*bp++ = L' '; *bp++ = 0;
	yylval.cp = setsymtab(buf, s, 0.0, CON|STR|DONTFREE, symtab);
	RET(STRING);
}


int binsearch(char *w, Keyword *kp, int n)
{
	int cond, low, mid, high;

	low = 0;
	high = n - 1;
	while (low <= high) {
		mid = (low + high) / 2;
		if ((cond = strcmp(w, kp[mid].word)) < 0)
			high = mid - 1;
		else if (cond > 0)
			low = mid + 1;
		else
			return mid;
	}
	return -1;
}

int word(char *w) 
{
	Keyword *kp;
	int c, n;

	n = binsearch(w, keywords, nelem(keywords));
	kp = keywords + n;
	if (n != -1) {	/* found in table */
		yylval.i = kp->sub;
		switch (kp->type) {	/* special handling */
		case FSYSTEM:
			if (safe)
				SYNTAX( "system is unsafe" );
			RET(kp->type);
		case FUNC:
			if (infunc)
				SYNTAX( "illegal nested function" );
			RET(kp->type);
		case RETURN:
			if (!infunc)
				SYNTAX( "return not in function" );
			RET(kp->type);
		case VARNF:
			yylval.cp = nfloc;
			RET(VARNF);
		default:
			RET(kp->type);
		}
	}
	c = peek();	/* look for '(' */
	if (c != L'(' && infunc && (n=isarg(w)) >= 0) {
		yylval.i = n;
		RET(ARG);
	} else {
		yylval.cp = setsymtab(w, EMPTY, 0.0, STR|NUM|FCONV, symtab);
		if (c == L'(') {
			RET(CALL);
		} else {
			RET(VAR);
		}
	}
}

void startreg(void)	/* next call to yyles will return a regular expression */
{
	reg = 1;
}

int regexpr(void)
{
	Rune c;
	static char *buf = 0;
	static int bufsz = 500;
	char *bp;

	if (buf == 0 && (buf = (char *) malloc(bufsz)) == nil)
		FATAL("out of space for rex expr");
	bp = buf;
	for ( ; (c = input()) != L'/' && c != 0; ) {
		if (!adjbuf(&buf, &bufsz, bp-buf+UTFmax+2, 500, &bp, 0))
			FATAL("out of space for reg expr %.10s...", buf);
		if (c == L'\n') {
			SYNTAX( "newline in regular expression %.10s...", buf ); 
			unput(L'\n');
			break;
		} else if (c == L'\\') {
			*bp++ = L'\\'; 
			c = input();
			bp += runetochar(bp, &c);
		} else {
			bp += runetochar(bp, &c);
		}
	}
	*bp = 0;
	yylval.s = tostring(buf);
	unput(L'/');
	RET(REGEXPR);
}

/* low-level lexical stuff, sort of inherited from lex */

Rune	ebuf[300];
Rune	*ep = ebuf;
Rune	yysbuf[100];	/* pushback buffer */
Rune	*yysptr = yysbuf;
Biobuf	*yyin;

Rune input(void)	/* get next lexical input character */
{
	int n;
	Rune c;
	extern char *lexprog;

	if (yysptr > yysbuf)
		c = *--yysptr;
	else if (lexprog != nil) {	/* awk '...' */
		n = chartorune(&c, lexprog);
		if (c != 0 && c != Runeerror)
			lexprog += n;
	} else				/* awk -f ... */
		c = pgetc();
	if (c == L'\n')
		lineno++;
	else if (c == (Rune)Beof)
		c = 0;
	if (ep >= ebuf + nelem(ebuf))
		ep = ebuf;
	return *ep++ = c;
}

void unput(Rune c)	/* put lexical character back on input */
{
	if (c == L'\n')
		lineno--;
	if (yysptr >= yysbuf + nelem(yysbuf))
		FATAL("pushed back too much: %.20s...", yysbuf);
	*yysptr++ = c;
	if (--ep < ebuf)
		ep = ebuf + nelem(ebuf) - 1;
}

void unputstr(char *s)	/* put a string back on input */
{
	char *p;
	Rune c;

	for (p = s + strlen(s) - 1; p >= s; p--) {
		chartorune(&c, p);
		if(c != Runeerror)
			unput(c);
	}
}
