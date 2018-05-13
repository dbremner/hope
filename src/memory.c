#include "defs.h"
#include "memory.h"
#include "error.h"
#include "align.h"

/* size of space for compiled code, tables, stack and heap */
#define	MEGABYTE (1024*1024L)
#define	MEMSIZE 16*MEGABYTE

/*
 * The granularity of allocation is the alignment granularity.
 */
#define	RoundDown(n)	((n)/ALIGNMENT*ALIGNMENT)
#define	RoundUp(n)	RoundDown((n) + (ALIGNMENT-1))

char	*base_memory, *top_memory;
char	*top_string, *base_table, *base_temp;
char	*lim_temp;

void
init_memory(void)
{
	if ((base_memory = (char *)malloc((size_t)MEMSIZE)) == nullptr)
		error(FATALERR, "can't allocate memory");
	top_memory = base_memory + RoundDown(MEMSIZE);

	lim_temp = top_string = base_memory;
	base_table = base_temp = top_memory;
}

void *
s_alloc(Natural n)
{
	auto start = top_string;
	top_string += RoundUp(n);
	lim_temp = top_string;
	if (base_temp < lim_temp)
		error(FATALERR, "can't store string: out of memory");
	return (void *)start;
}

void *
t_alloc(size_t n)
{
	base_temp -= RoundUp(n);
	if (base_temp < lim_temp)
		error(FATALERR, "out of memory");
	return (void *)base_temp;
}

void
clean_slate(void)
{
	base_temp = base_table;
	lim_temp = top_string;
}

void
preserve(void)
{
	base_table = base_temp;
	lim_temp = top_string;
}
