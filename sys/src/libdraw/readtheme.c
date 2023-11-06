#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>

int
readtheme(Theme *col, int ncol, char *file)
{
	int i;
	char *s, *v[3];
	Biobuf *bf;

	if(col == nil || ncol <= 0){
		werrstr("no color to hand");
		return -1;
	}
	for(i=0; i<ncol; i++)
		if(col[i].name == nil){
			werrstr("invalid color at index %d", i);
			return -1;
		}
	if(file == nil)
		file = "/dev/theme";
	if((bf = Bopen(file, OREAD)) == nil)
		return -1;
	while((s = Brdline(bf, '\n')) != nil){
		s[Blinelen(bf)-1] = 0;
		if(tokenize(s, v, nelem(v)) <= 0)
			continue;
		for(i=0; i<ncol; i++)
			if(strcmp(v[0], col[i].name) == 0)
				col[i].c = strtoul(v[1], nil, 16)<<8 | 0xff;
	}
	Bterm(bf);
	return 0;
}
