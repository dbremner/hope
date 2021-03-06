/*
 *	Source file management and error reporting.
 */
#include "defs.h"
#include "source.h"
#include "module.h"
#include "newstring.h"
#include "error.h"
#include "interrupt.h"
#include "exceptions.h"

#define	MAX_DEPTH	16	/* max. depth of module nesting (checked) */
#define	MAX_LINE	1024	/* max. length of input line (checked) */

#define	PROMPTOUT	stdout

FILE	*errout;

static Bool	gen_listing;	/* generate a listing */

static Bool	atend;

typedef	struct {
	FILE	*file;
	int	lineno;
} Source;

static Source	source[MAX_DEPTH];
static Source	*cur_source;

static Bool	istty;		/* top level input is a terminal */

static char	src_line[MAX_LINE];
const	Byte	*inptr;

static const	char	prompt[] = ">: ";	/* the Hope command prompt */

#ifdef RE_EDIT
#define	MAX_COMMAND	100	/* max. length of sh command (not checked) */
#define	MAX_FILENAME	100	/* max. length of file name (not checked) */

/*
 * To restart Hope after an edit, arising either from an "edit"
 * command or from an error in a source file (see below), we dump
 * the current session into a temporary file.  On restart, this file,
 * called a "script", is read first before reading the standard input.
 * Errors in the script cause the editor to be re-invoked (see below).
 */

static const	char	tmp_stem[] = "/tmp/hope";
#define	MAX_TEMP	(sizeof(tmp_stem)+6)

static const	char	*script;
#define	in_script()	(cur_source == source+1 && script != nullptr)

/*
 * If an error is encountered while reading a file, a listing file
 * is created, consisting of the text of the source file with flagged
 * error messages embedded in it.  When the end of the file is reached,
 * The editor is invoked on the listing file.  If the user exits without
 * changing the listing, they leave Hope.  Otherwise, the source file
 * is replaced by the listing, minus error messages, and Hope attempts
 * to return to the previous state.
 */

/* extension for listing files of Hope modules */
static const	char	list_extension[] = ".LST";

static int	first_error;	/* line no. of first error */
static char	list_file[MAX_FILENAME];

static time_t	mtime(const char *file);
static void	re_edit_script(void);
static void	get_script(char *buf, size_t len);
static void	temp_name(char *buf, size_t len);
static void	head(int lines, const char *filename, FILE *to);
static void	remove_messages(const char *fromname, const char *toname);
#endif

/*
 * The prefix with which error messages are annotated in a listing.
 * Lines starting with this prefix are removed after the listing has been
 * edited, to obtain the new version of the source file.  The prefix
 * should not normally occur at the start of a line in a Hope program.
 */
static const	char	list_prefix[] = "@ ";

/* level (if any) at which we are creating a listing file */
static Source	*listing;

void
init_source(FILE *src, Bool listing_flag)
{
	gen_listing = listing_flag;

	cur_source = source-1;
	atend = FALSE;
	enterfile(src);
#ifdef	unix
	istty = isatty(fileno(src));
#else
	istty = src == stdin;
#endif
	if (istty)
		disable_interrupt();
#ifdef RE_EDIT
	script = nullptr;
#endif
	listing = gen_listing ? cur_source : nullptr;
	errout = stderr;
}

void
start_err_line(void)
{
#ifdef	RE_EDIT
	if (listing != cur_source && ! gen_listing) {
		if (listing != nullptr) {
			/*
			 * We were already listing, but now we
			 * have an error in a deeper module --
			 * abandon the old listing.
			 */
			(void)fclose(errout);
			(void)remove(list_file);
			listing = nullptr;
		}
		/* if it's in a system module, it will be fatal */
		if (istty && cur_source > source && ! mod_system()) {
			char	input_file[MAX_FILENAME];

			if (in_script()) {
				(void)snprintf(input_file, sizeof(input_file), "%s", script);
				(void)snprintf(list_file, sizeof(list_file), "/tmp/hopeXXXXXX");
				(void)mktemp(list_file);
			} else {
				mod_file(input_file, sizeof(input_file), mod_name());
				(void)snprintf(list_file, sizeof(list_file), "%s%s",
					mod_name(), list_extension);
			}
			first_error = cur_source->lineno;
			if ((errout = fopen(list_file, "w")) == nullptr) {
				errout = stderr;
				error(FATALERR, "can't create listing file");
			}
			listing = cur_source;
			head(first_error, input_file, errout);
		} else
			errout = stderr;
	}
#else
	errout = stderr;
#endif
	if (listing != nullptr)
		(void)fprintf(errout, list_prefix);
}

/*
 *	Called on detecting the error -- flag the erroneous token.
 */
/*ARGSUSED*/
void
yyerror(const char *s)
{
	const	Byte	*p;
	int	column;

	if (strcmp(s, "syntax error") == 0) {
		start_err_line();
		if (interactive())
			column = (int)sizeof(prompt)-1;
		else {
			/* line ends in a newline */
			if (listing == nullptr) {
				(void)fprintf(errout, "%s", src_line);
				start_err_line();
			}
			column = 0;
		}
		for (p = (const Byte *)src_line; p < inptr; p++)
			if (*p >= ' ')
				column++;
			else if (*p == '\t')
				column += 8 - column%8;
		if (listing != nullptr) {
			column -= (int)sizeof(list_prefix)-1;
			if (column < 0)
				column = 0;
		}
		(void)fprintf(errout, "%*s\n", column, "^");
		error(SYNERR, (const char *)0);
	} else
		error(SYNERR, s);
}

Bool	erroneous;

#include <stdarg.h>
#define	VAR_FPRINTF(f,fmt) {\
		va_list __args__;\
		va_start(__args__, fmt);\
		(void)vfprintf(f, fmt, __args__);\
		va_end(__args__);\
	}

void
error(int errtype, const char *fmt, ...)
{
/* array indexed by error types (see error.h) */
static	const	char	*const	errname[] = {
			"lexical error",	/* LEXERR */
			"syntax error",		/* SYNERR */
			"semantic error",	/* SEMERR */
			"type error",		/* TYPEERR */
			"run-time error",	/* EXECERR */
			"user error",		/* USERERR */
			"fatal error",		/* FATALERR */
			"library error",	/* LIBERR */
			"internal error"	/* INTERR */
		};

	/* fatal errors don't go in an edit listing */
	if (errtype < FATALERR || gen_listing)
		start_err_line();
	else  {
		if (listing != nullptr) {
			/* so get rid of any listing in that case */
			(void)fclose(errout);
#ifdef RE_EDIT
			(void)remove(list_file);
#endif
			listing = nullptr;
		}
		errout = stderr;
	}
	if (listing != cur_source && ! interactive()) {
		if (cur_source > source)
			(void)fprintf(errout, "module %s, ", mod_name());
		if (cur_source >= source)
			(void)fprintf(errout, "line %d: ", cur_source->lineno);
	}
	(void)fprintf(errout, "%s", errname[errtype]);
	if (fmt != nullptr) {
		(void)fprintf(errout, " - ");
		VAR_FPRINTF(errout, fmt);
	}
	(void)fprintf(errout, "\n");

	if (errtype == INTERR)
		abort();
	if (errtype >= FATALERR) {
		if (gen_listing)	/* copy the rest to the listing */
			while (_getline())
				;
		exit(1);
	}
	if (mod_system())
		error(LIBERR, "error in system module");
	if (errtype >= TYPEERR) {
		if (istty)
			disable_interrupt();
		longjmp(execerror, 1);
	}
	erroneous = TRUE;
}

void
enterfile(FILE *f)
{
	cur_source++;
	if (cur_source == source + MAX_DEPTH)
		error(FATALERR, "modules nested too deeply");
	cur_source->file = f;
	cur_source->lineno = 0;
	inptr = (const Byte *)"";
}

Bool
interactive(void)
{
	return cur_source == source && istty;
}

/*
 *	Get a line from the current input.
 *	The line will be terminated by a null if it comes from the terminal;
 *	otherwise it ends in a newline (whitespace) and then a null.
 */
Bool
_getline(void)
{
	if (atend && cur_source >= source) {
		if (cur_source > source)
			(void)fclose(cur_source->file);
#ifdef	RE_EDIT
		if (listing == cur_source && ! gen_listing) {
			(void)fclose(errout);
			re_edit_script();
		}
		if (in_script()) {
			(void)remove(script);
			script = nullptr;
			cur_source--;
		}
		else
#endif
		if (cur_source > source) {
			cur_source--;
			mod_finish();
		}
		else
			cur_source--;
		atend = FALSE;
	}
	if (cur_source < source)
		return FALSE;

	if (interactive()) {
		(void)fprintf(PROMPTOUT, prompt);
		(void)fflush(PROMPTOUT);
		atend = fgets(src_line, sizeof(src_line),
				cur_source->file) == nullptr;
		if (atend) {
			(void)fprintf(PROMPTOUT, "\n");
			(void)fflush(PROMPTOUT);
		}
	}
	else {
		atend = fgets(src_line, sizeof(src_line),
				cur_source->file) == nullptr;
#ifdef unix
		/* hack for Unix script files */
		if (! atend &&
		    cur_source == source && cur_source->lineno == 0 &&
		    strncmp(src_line, "#!", 2) == 0) {
			cur_source->lineno++;
			atend = fgets(src_line, sizeof(src_line),
					cur_source->file) == nullptr;
		}
#endif
		if (listing == cur_source && ! atend) {
			(void)fprintf(errout, "%s", src_line);
			if (src_line[strlen(src_line)-1] != '\n')
				(void)fprintf(errout, "\n");
		}
	}
	/*
	 * Insert a line containing only a semi-colon at the end of each file,
	 * to end any un-terminated line;
	 */
	if (atend) {
		src_line[0] = '\0';
		inptr = (const Byte *)(interactive() ? ";" : ";\n");
	}
	else
		inptr = (const Byte *)src_line;
	cur_source->lineno++;
	return TRUE;
}

#ifdef RE_EDIT
/*
 *	Copy the indicated number of initial lines from filename into "to".
 */
static void
head(int lines, const char *filename, FILE *to)
{
	FILE	*from;
	int	c;

	if ((from = fopen(filename, "r")) == nullptr)
		error(FATALERR, "'%s': can't read file", filename);
	while (lines-- > 0)
		do {	/* copy a line */
			c = getc(from);
			if (c == EOF) {	/* premature EOF */
				putc('\n', to);
				fclose(from);
				return;
			}
			putc(c, to);
		} while (c != '\n');
	fclose(from);
}

/*
 *	`edit' command: save the current definitions in a file,
 *	invoke an editor on the file, and then re-enter Hope,
 *	re-reading the file.
 *	If an error occurs during re-reading, go back to the editor.
 */
void
edit(String name)
{
	char	tmp_file[MAX_TEMP];
	char	filename[MAX_FILENAME];
	char	command[MAX_COMMAND];

	if (restricted) {
		error(SEMERR, "'edit' command disabled");
		return;
	}
	if (! interactive())
		return;
	get_script(tmp_file, sizeof(tmp_file));
	if (name != nullptr)
		mod_file(filename, sizeof(filename), name);
	else
		(void)snprintf(filename, sizeof(filename), "%s", tmp_file);
	(void)snprintf(command, sizeof(command), "${EDITOR-vi} %s", filename);
	(void)system(command);
	restart(tmp_file);
}

void
set_script(const char *filename)
{
	FILE	*f;

	if (strncmp(filename, tmp_stem, (int)sizeof(tmp_stem)-1) != 0)
		error(FATALERR, "'%s': bad argument", filename);
	else if ((f = fopen(filename, "r")) == nullptr)
		error(FATALERR, "'%s': can't read script", filename);
	else {
		script = filename;
		enterfile(f);
	}
}

static void
get_script(char *buf, size_t len)
{
	if (script)
        (void)snprintf(buf, len, "%s", script);
	else {
		temp_name(buf, len);
		mod_dump(fopen(buf, "w"));
	}
}

static void
temp_name(char *buf, size_t len)
{
	(void)snprintf(buf, len, "%sXXXXXX", tmp_stem);
	(void)mktemp(buf);
}

#include <sys/types.h>
#include <sys/stat.h>

static time_t
mtime(const char *file)
{
	struct	stat	buf;

	if (stat(file, &buf) < 0)
		return 0;
	return buf.st_mtime;
}

static void
re_edit_script(void)
{
	char	command[MAX_COMMAND];
	char	filename[MAX_FILENAME];
	char	tmp_file[MAX_TEMP];
	time_t	before;

	get_script(tmp_file, sizeof(tmp_file));
	before = mtime(list_file);
	(void)snprintf(command, sizeof(command), "${EDITOR-vi} +%d %s", first_error, list_file);
	(void)system(command);
	if (mtime(list_file) == before) {	/* no change */
		(void)remove(list_file);
		(void)remove(tmp_file);
		exit(0);
	}
	if (in_script())
		(void)snprintf(filename, sizeof(filename), "%s", tmp_file);
	else
		mod_file(filename, sizeof(filename), mod_name());
	remove_messages(list_file, filename);
	(void)remove(list_file);
	restart(tmp_file);
}

static void
remove_messages(const char *fromname, const char *toname)
{
	auto from = fopen(fromname, "r");
	auto to = fopen(toname, "w");

	if (from == nullptr)
		error(FATALERR, "'%s': can't read listing file", fromname);
	if (to == nullptr)
		error(FATALERR, "'%s': can't overwrite file", toname);
	while (fgets(src_line, sizeof(src_line), from) != nullptr)
		if (strncmp(src_line, list_prefix, sizeof(list_prefix)-1) != 0)
			(void)fprintf(to, "%s", src_line);
	fclose(from);
	fclose(to);
}
#endif
