#include "defs.h"
#include "stream.h"
#include "error.h"
#include "expr.h"
#include "cons.h"
#include "value.h"
#include "builtin.h"

#define	MAX_STREAMS	20	/* max. no. of streams (checked) */
#define	MAX_FILENAME	100	/* max. len. of file name (checked) */
#define	MAX_INPUTLINE	256	/* max. len. of tty input line (checked) */

/*
 * Table of open streams,
 * so we can close any left open at the end of evaluation.
 */
static FILE	*str_table[MAX_STREAMS];

static int	get_one_char(void);
static void	end_stream(FILE *f);

Cell *
open_stream(Cell *arg)
{
	char	filename[MAX_FILENAME];
	FILE	**fp;

	if (restricted)
		error(EXECERR, "read function disabled");

	hope2c((Byte *)filename, MAX_FILENAME, arg);

	/* find a free slot in the stream table */
	for (fp = str_table; *fp != nullptr; fp++)
		if (fp == &str_table[MAX_STREAMS])
			error(EXECERR, "stream table full");

	/* try to open the file */
	if ((*fp = fopen(filename, "r")) == nullptr)
		error(EXECERR, "'%s': can't read file", filename);
	return new_stream(*fp);
}

Cell *
read_stream(Cell *cell)
{
	long	c;

	c = cell->c_file == stdin ? get_one_char() : GetChar(cell->c_file);
	if (c == EOF) {
		end_stream(cell->c_file);
		return new_cnst(nil);
	}
	return new_cons(cons,
		new_pair(new_char((Char)c), new_stream(cell->c_file)));
}

static char	str_line[MAX_INPUTLINE];
static const	Byte	*str_lptr;

void
reset_streams(void)
{
	FILE	**fp;

	str_lptr = (const Byte *)"";
	for (fp = str_table; fp != &str_table[MAX_STREAMS]; fp++)
		*fp = nullptr;
}

/*
 * Line-buffering for standard input, because if not all the input is used,
 * we want to discard the rest of the last line input.
 */
static int
get_one_char(void)
{
	if (*str_lptr == '\0') {
		if (fgets(str_line, sizeof(str_line), stdin) == nullptr) {
			clearerr(stdin);
			return EOF;
		}
		str_lptr = (const Byte *)str_line;
	}
	return FetchChar(&str_lptr);
}

static void
end_stream(FILE *f)
{
	FILE	**fp;

	if (f != stdin) {
		(void)fclose(f);
		for (fp = str_table; *fp != f; fp++)
			;
		*fp = nullptr;
	}
}

void
close_streams(void)
{
	FILE	**fp;

	for (fp = str_table; fp != &str_table[MAX_STREAMS]; fp++)
		if (*fp != nullptr) {
			(void)fclose(*fp);
			*fp = nullptr;
		}
}
