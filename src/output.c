/*
 *	The builtins "print" and "write_element".
 */

#include "defs.h"
#include "output.h"
#include "expr.h"
#include "cases.h"
#include "type_value.h"
#include "type_check.h"
#include "pr_ty_value.h"
#include "value.h"
#include "pr_value.h"
#include "memory.h"
#include "error.h"

#define	STDOUT	stdout

Expr	*e_return, *e_print, *e_wr_list;

void
init_print(void)
{
	e_return = NEW(Expr);
    e_return->e_class = expr_type::E_RETURN;
	auto fn = fn_lookup(newstring("return"));
	ASSERT( fn != nullptr );
	fn->f_code = success(e_return, 0);

	fn = fn_lookup(newstring("print"));
	ASSERT( fn != nullptr );
	e_print = NEW(Expr);
	e_print->e_class = expr_type::E_DEFUN;
	e_print->e_defun = fn;

	fn = fn_lookup(newstring("write_list"));
	ASSERT( fn != nullptr );
	e_wr_list = NEW(Expr);
	e_wr_list->e_class = expr_type::E_DEFUN;
	e_wr_list->e_defun = fn;
}

/*
 *	Print value and inferred type on standard output
 */
Cell *
print_value(Cell *value)
{
	(void)fprintf(STDOUT, ">> ");
	pr_value(STDOUT, value);
	(void)fprintf(STDOUT, " : ");
	pr_ty_value(STDOUT, expr_type);
	(void)fprintf(STDOUT, "\n");
	return new_susp(e_return, NOCELL);
}

/*
 *	Direct a list-valued output to the terminal or a file
 */

static FILE	*out_file;
static const	char	*out_name;

#define	TEMPFILE "TempFile"

void
open_out_file(const char *name)
{
	if (restricted)
		error(EXECERR, "file output disabled");
	if (name == nullptr)
		out_file = STDOUT;
	else if ((out_file = fopen(TEMPFILE, "w")) == nullptr)
		error(EXECERR, "can't create temporary file");
	out_name = name;
}

void
save_out_file(void)
{
	if (out_name != nullptr) {
		(void)fclose(out_file);
		(void)remove(out_name);
		/* (void)link(TEMPFILE, out_name); (void)unlink(TEMPFILE); */
		(void)rename(TEMPFILE, out_name);
	}
}

void
close_out_file(void)
{
	if (out_name != nullptr) {
		(void)fclose(out_file);
		(void)remove(TEMPFILE);
	}
}

Cell *
write_value(Cell *value)
{
	if (value->c_class == C_CHAR)
		PutChar(value->c_char, out_file);
	else {
		pr_value(out_file, value);
		(void)fprintf(out_file, "\n");
	}
	return new_susp(e_wr_list, NOCELL);
}
