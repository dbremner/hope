#include "defs.h"
#include "eval.h"
#include "expr.h"
#include "error.h"
#include "compile.h"
#include "interpret.h"
#include "stream.h"
#include "number.h"
#include "output.h"
#include "type_check.h"
#include "exceptions.h"

static Bool	create_environment(Expr *expr);

/*
 *	Evaluation of expressions.
 */

jmp_buf	execerror;

static Bool
create_environment(Expr *expr)
{
	return nr_branch(new_unary(id_expr(newstring("input")),
			expr,
			nullptr));
}

void
eval_expr(Expr *expr)
{
	if (erroneous)
		return;
	if (create_environment(expr)) {
		reset_streams();
		if (! setjmp(execerror)) {
			chk_expr(expr);
			comp_expr(expr);
			interpret(e_print, expr);
		}
		close_streams();
	}
}

void
wr_expr(Expr *expr, const char *file)
{
	if (erroneous)
		return;
	if (create_environment(expr)) {
		reset_streams();
		if (! setjmp(execerror)) {
			open_out_file(file);
			chk_list(expr);
			comp_expr(expr);
			interpret(e_wr_list, expr);
			save_out_file();
		} else
			close_out_file();
		close_streams();
	}
}
