#include "defs.h"
#include "pr_expr.h"
#include "expr.h"
#include "cons.h"
#include "op.h"
#include "print.h"
#include "pr_value.h"
#include "names.h"

static void	pr_f_expr(FILE *f, String name, Expr *arg,
				int level, int context);
static Bool	is_list(Expr *expr);
static void	pr_elist(FILE *f, Expr *expr, int level);
static Bool	is_string(Expr *expr);
static void	pr_string(FILE *f, Expr *expr);
static void	pr_lambda(FILE *f, Branch *branch, int level);
static void	pr_formals(FILE *f, Expr *formals);
static void	pr_presection(FILE *f, Expr *expr, int level);
static void	pr_postsection(FILE *f, Expr *expr, int level);
static int	precedence(Expr *expr);

static Bool	in_definition;		/* initially FALSE */

/*
 *	Printing of functions.
 */

void
pr_fundef(FILE *f, Func *fn)
{
	Branch	*br;

	in_definition = TRUE;
	for (br = fn->f_branch; br != nullptr; br = br->br_next) {
		(void)fprintf(f, "%s ", n_valof);
		pr_c_expr(f, br->br_formals, 0, PREC_BODY);
		(void)fprintf(f, " %s ", n_is);
		pr_c_expr(f, br->br_expr, fn->f_arity, PREC_BODY);
		(void)fprintf(f, ";\n");
	}
	in_definition = FALSE;
}

/*
 *	Printing of expressions.
 *
 *	level = no. of environment levels supplied by the expression.
 *	Others are fetched from the current environment with get_actual().
 */

void
pr_expr(FILE *f, Expr *expr)
{
	pr_c_expr(f, expr, MAX_SCOPES, PREC_BODY);
}

void
pr_c_expr(FILE *f, Expr *expr, int level, int context)
{
	int	prec;
	String	name;

	prec = precedence(expr);
	if (prec < context)
		(void)fprintf(f, "(");
	switch (expr->e_class) {
	case expr_type::E_PAIR:
		pr_c_expr(f, expr->e_left, level, PREC_COMMA+1);
		(void)fprintf(f, ", ");
		pr_c_expr(f, expr->e_right, level, PREC_COMMA);
        break;
    case expr_type::E_APPLY:
		if (is_list(expr))
			if (is_string(expr))
				pr_string(f, expr);
			else
				pr_elist(f, expr, level);
		else {
			name = expr_name(expr->e_func, level);
			if (name != nullptr)
				pr_f_expr(f, name, expr->e_arg,
					level, InnerPrec(prec, context));
			else {
				pr_c_expr(f, expr->e_func, level, PREC_APPLY);
				(void)fprintf(f, " ");
				pr_c_expr(f, expr->e_arg, level, PREC_ARG);
			}
		}
        break;
    case expr_type::E_IF:
		(void)fprintf(f, "%s ", n_if);
		pr_c_expr(f, expr->e_func->e_func->e_arg, level, PREC_BODY);
		(void)fprintf(f, " %s ", n_then);
		pr_c_expr(f, expr->e_func->e_arg, level, PREC_BODY);
		(void)fprintf(f, " %s ", n_else);
		pr_c_expr(f, expr->e_arg, level, PREC_IF);
        break;
    case expr_type::E_LET:
		(void)fprintf(f, "%s ", n_let);
		pr_c_expr(f, expr->e_func->e_branch->br_formals->e_arg,
			level+1, PREC_BODY);
		(void)fprintf(f, " %s ", n_eq);
		pr_c_expr(f, expr->e_arg, level, PREC_BODY);
		(void)fprintf(f, " %s ", n_in);
		pr_c_expr(f, expr->e_func->e_branch->br_expr,
			level+1, PREC_LET);
        break;
    case expr_type::E_RLET:
		(void)fprintf(f, "%s ", n_letrec);
		pr_c_expr(f, expr->e_func->e_branch->br_formals->e_arg,
			level+1, PREC_BODY);
		(void)fprintf(f, " %s ", n_eq);
		pr_c_expr(f, expr->e_arg, level+1, PREC_BODY);
		(void)fprintf(f, " %s ", n_in);
		pr_c_expr(f, expr->e_func->e_branch->br_expr,
			level+1, PREC_LET);
        break;
    case expr_type::E_WHERE:
		pr_c_expr(f, expr->e_func->e_branch->br_expr,
			level+1, PREC_WHERE);
		(void)fprintf(f, " %s ", n_where);
		pr_c_expr(f, expr->e_func->e_branch->br_formals->e_arg,
			level+1, PREC_BODY);
		(void)fprintf(f, " %s ", n_eq);
		pr_c_expr(f, expr->e_arg, level, PREC_WHERE);
        break;
    case expr_type::E_RWHERE:
		pr_c_expr(f, expr->e_func->e_branch->br_expr,
			level+1, PREC_WHERE);
		(void)fprintf(f, " %s ", n_whererec);
		pr_c_expr(f, expr->e_func->e_branch->br_formals->e_arg,
			level+1, PREC_BODY);
		(void)fprintf(f, " %s ", n_eq);
		pr_c_expr(f, expr->e_arg, level+1, PREC_WHERE);
        break;
    case expr_type::E_MU:
		(void)fprintf(f, "%s ", n_mu);
		pr_formals(f, expr->e_muvar);
		(void)fprintf(f, " %s ", n_gives);
		pr_c_expr(f, expr->e_body, level+1, PREC_MU);
        break;
    case expr_type::E_LAMBDA:
		pr_lambda(f, expr->e_branch, level + expr->e_arity);
        break;
    case expr_type::E_PRESECT:
		if (in_definition)
			pr_presection(f, expr->e_branch->br_expr, level+1);
		else
			pr_lambda(f, expr->e_branch, level+1);
        break;
    case expr_type::E_POSTSECT:
		if (in_definition)
			pr_postsection(f, expr->e_branch->br_expr, level+1);
		else
			pr_lambda(f, expr->e_branch, level+1);
        break;
    case expr_type::E_NUM:
		(void)fprintf(f, NUMfmt, expr->e_num);
        break;
    case expr_type::E_CHAR:
		(void)fprintf(f, "'");
		pr_char(f, expr->e_char);
		(void)fprintf(f, "'");
        break;
    case expr_type::E_DEFUN:
		(void)fprintf(f, "%s", expr->e_defun->f_name);
        break;
    case expr_type::E_CONS:
		if (expr == e_nil)
			(void)fprintf(f, "[]");
		else
			(void)fprintf(f, "%s", expr->e_const->c_name);
        break;
    case expr_type::E_PARAM:
		if (expr->e_level < level)
			pr_c_expr(f, expr->e_patt,
				0, InnerPrec(prec, context));
		else
			pr_actual(f, expr->e_level - level,
				expr->e_where, InnerPrec(prec, context));
        break;
    case expr_type::E_PLUS:
		pr_c_expr(f, expr->e_rest, level, prec);
		(void)fprintf(f, " + %d", expr->e_incr);
        break;
    case expr_type::E_VAR:
		(void)fprintf(f, "%s", expr->e_vname);
        break;
    default:
		NOT_REACHED;
	}
	if (prec < context)
		(void)fprintf(f, ")");
}

static void
pr_f_expr(FILE *f, String name, Expr *arg, int level, int context)
{
	Op	*op;

	if (arg->e_class == expr_type::E_PARAM)
		if (arg->e_level < level)
			pr_f_expr(f, name, arg->e_patt, 0, context);
		else
			pr_f_actual(f, name,
				arg->e_level - level, arg->e_where, context);
	else if ((op = op_lookup(name)) != nullptr)
		if (arg->e_class == expr_type::E_PAIR) {
			if (op->op_prec < context)
				(void)fprintf(f, "(");
			pr_c_expr(f, arg->e_left, level, LeftPrec(op));
			(void)fprintf(f, " %s ", name);
			pr_c_expr(f, arg->e_right, level, RightPrec(op));
			if (op->op_prec < context)
				(void)fprintf(f, ")");
		} else {
			(void)fprintf(f, "(%s) ", name);
			pr_c_expr(f, arg, level, PREC_ARG);
		}
	else {
		(void)fprintf(f, "%s ", name);
		pr_c_expr(f, arg, level, PREC_ARG);
	}
}

/*
 * An expression is printed as a list if it was input as a list,
 * signified by being built with e_cons (and e_nil).
 */
static Bool
is_list(Expr *expr)
{
	return expr->e_class == expr_type::E_APPLY && expr->e_func == e_cons;
}

static void
pr_elist(FILE *f, Expr *expr, int level)
{
	(void)fprintf(f, "[");
	for(;;) {
		pr_c_expr(f, expr->e_arg->e_left, level, PREC_COMMA+1);
		expr = expr->e_arg->e_right;
        if(expr->e_const == nil) break;
		(void)fprintf(f, ", ");
	}
	(void)fprintf(f, "]");
}

/*
 *	Is expr a string?  (We already know it's a list)
 */
static Bool
is_string(Expr *expr)
{
	while (expr->e_class == expr_type::E_APPLY &&
	       expr->e_arg->e_left->e_class == expr_type::E_CHAR)
		expr = expr->e_arg->e_right;
	return expr->e_class == expr_type::E_CONS;		/* i.e. nil */
}

static void
pr_string(FILE *f, Expr *expr)
{
	(void)fprintf(f, "\"");
	while (expr->e_const != nil) {
		pr_char(f, expr->e_arg->e_left->e_char);
		expr = expr->e_arg->e_right;
	}
	(void)fprintf(f, "\"");
}

void
pr_char(FILE *f, Char c)
{
	switch (c) {
	case '\007':	(void)fprintf(f, "\\a");
        break;
    case '\b':	(void)fprintf(f, "\\b");
        break;
    case '\f':	(void)fprintf(f, "\\f");
        break;
    case '\n':	(void)fprintf(f, "\\n");
        break;
    case '\r':	(void)fprintf(f, "\\r");
        break;
    case '\t':	(void)fprintf(f, "\\t");
        break;
    case '\013':	(void)fprintf(f, "\\v");
        break;
    default:
		if (IsCntrl(c))
			(void)fprintf(f, "\\%03o", c);
		else
			PutChar(c, f);
	}
}

static void
pr_lambda(FILE *f, Branch *branch, int level)
{
	(void)fprintf(f, "%s ", n_lambda);
	while (branch != nullptr) {
		pr_branch(f, branch, level);
		branch = branch->br_next;
		if (branch != nullptr)
			(void)fprintf(f, " | ");
	}
}

void
pr_branch(FILE *f, Branch *branch, int level)
{
	pr_formals(f, branch->br_formals);
	(void)fprintf(f, "%s ", n_gives);
	pr_c_expr(f, branch->br_expr, level, PREC_LAMBDA);
}

static void
pr_formals(FILE *f, Expr *formals)
{
	if (formals != nullptr && formals->e_class == expr_type::E_APPLY) {
		pr_formals(f, formals->e_func);
		pr_c_expr(f, formals->e_arg, 0, PREC_FORMAL);
		(void)fprintf(f, " ");
	}
}

static void
pr_presection(FILE *f, Expr *expr, int level)
{
	pr_c_expr(f, expr->e_arg->e_left, level, PREC_COMMA+1);
	(void)fprintf(f, " %s", expr->e_func->e_class == expr_type::E_DEFUN ?
				expr->e_func->e_defun->f_name :
				expr->e_func->e_const->c_name);
}

static void
pr_postsection(FILE *f, Expr *expr, int level)
{
	(void)fprintf(f, "%s ", expr->e_func->e_class == expr_type::E_DEFUN ?
				expr->e_func->e_defun->f_name :
				expr->e_func->e_const->c_name);
	pr_c_expr(f, expr->e_arg->e_right, level, PREC_COMMA+1);
}

/*
 *	If expr amounts to an identifier, return it, else NULL.
 */
String
expr_name(Expr *expr, int level)
{
	switch (expr->e_class) {
	case expr_type::E_DEFUN:
		return expr->e_defun->f_name;
    case expr_type::E_CONS:
		return expr->e_const->c_name;
    case expr_type::E_PLUS:
		return newstring("+");
    case expr_type::E_VAR:
		return expr->e_vname;
    case expr_type::E_PARAM:
		if (expr->e_level < level)
			return expr_name(expr->e_patt, 0);
		return val_name(expr->e_level - level, expr->e_where);
	default:
		return nullptr;
	}
}

static int
precedence(Expr *expr)
{
	switch (expr->e_class) {
	case expr_type::E_NUM:
    case expr_type::E_CHAR:
		return PREC_ATOMIC;
    case expr_type::E_PAIR:
		return PREC_COMMA;
    case expr_type::E_LAMBDA:
		return PREC_LAMBDA;
    case expr_type::E_MU:
		return PREC_MU;
    case expr_type::E_PRESECT:
    case expr_type::E_POSTSECT:
		return in_definition ? PREC_INFIX : PREC_LAMBDA;
    case expr_type::E_WHERE:
    case expr_type::E_RWHERE:
		return PREC_WHERE;
    case expr_type::E_LET:
    case expr_type::E_RLET:
		return PREC_LET;
    case expr_type::E_IF:
		return PREC_IF;
    case expr_type::E_APPLY:
		return PREC_APPLY;
    case expr_type::E_CONS:
		if (op_lookup(expr->e_const->c_name) != nullptr)
			return PREC_INFIX;
		else
			return PREC_ATOMIC;
    case expr_type::E_DEFUN:
		if (op_lookup(expr->e_defun->f_name) != nullptr)
			return PREC_INFIX;
		else
			return PREC_ATOMIC;
    case expr_type::E_PLUS:
		return op_lookup(newstring("+"))->op_prec;
    case expr_type::E_VAR:
		if (op_lookup(expr->e_vname) != nullptr)
			return PREC_INFIX;
		else
			return PREC_ATOMIC;
    case expr_type::E_PARAM:
		return precedence(expr->e_patt);
	default:
		NOT_REACHED;
	}
}
