#include "defs.h"
#include "path.h"
#include "memory.h"

#define	MAX_PATH	40	/* max. length of a path (not checked) */

/*
 *	Create a new, empty path.
 *	Returned value points to a static area that will be overwritten
 *	by the next call.
 */
Path
p_new(void)
{
static	char	path_buf[MAX_PATH];

	path_buf[MAX_PATH-1] = P_END;
	return &path_buf[MAX_PATH-1];
}

Path
p_push(int dir, Path p)
{
	*--p = dir;
	return p;
}

/* permanent storage for a path */
Path
p_stash(Path p)
{
    const Natural len = strlen(p) + 1;
    char *dest = NEWARRAY(char, len);
	return strcpy(dest, p);
}

/* temporary storage for a number of paths */
static char	*p_buffer;
static char	*pb_end;
static int	pb_size;	/* not checked at present */

void
p_init(char *buf, int size)
{
	pb_end = p_buffer = buf;
	pb_size = size;
}

Path
p_save(Path p)
{
	char	*new;

	new = strcpy(pb_end, p);
	pb_end += strlen(p)+1;
	return new;
}

/*
 *	Reverse a path, adding an UNROLL before each direction in the initial
 *	string of LEFTs and RIGHTs.
 *	Returned value points to a static area that will be overwritten
 *	by the next call.
 */
Path
p_reverse(Path old)
{
static	char	path_buf[MAX_PATH];
	Path	new;
	int	dir;

	path_buf[MAX_PATH-1] = P_END;
	new = &path_buf[MAX_PATH-1];

	for(;;) {
        if(p_empty(old)) break;
		dir = p_top(old);
		new = p_push(dir, new);
		old = p_pop(old);
        if(dir != P_LEFT && dir != P_RIGHT) break;
		new = p_push(P_UNROLL, new);
	}
	while (! p_empty(old)) {
		new = p_push(p_top(old), new);
		old = p_pop(old);
	}
	return new;
}
