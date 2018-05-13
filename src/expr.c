#include "defs.h"
#include "expr.h"
#include "cons.h"
#include "memory.h"
#include "cases.h"
#include "number.h"
#include "compile.h"
#include "type_check.h"
#include "error.h"
#include "path.h"

/*
 *	Functions, Expressions and Patterns.
 *
 *	All semantic checks here occur at the top level, so sub-expressions
 *	are always defined.
 */

/* Internal names of some constructor expressions */
Expr	*e_true, *e_false, *e_cons, *e_nil;
Func	*f_id;

/* the following is different from any String */
static const	char	bound_variable[] = "x'";

Expr *
char_expr(Char c)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_CHAR;
	expr->e_char = c;
	return expr;
}

Expr *
text_expr(const Byte *text, int n)
{
	const	Byte	*s;
	Expr	*expr;

	expr = e_nil;
	for (s = text+n-1; s >= text; s--)
		expr = apply_expr(e_cons, pair_expr(char_expr(*s), expr));
	return expr;
}

Expr *
num_expr(Num n)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_NUM;
	expr->e_num = n;
	return expr;
}

Expr *
cons_expr(Cons *constr)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_CONS;
	expr->e_const = constr;
	return expr;
}

/*
 *	An identifier occurring as an expression.
 *	Call it a variable for now; we'll find out what it really is later.
 */
Expr *
id_expr(String name)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_VAR;
	expr->e_vname = name;
	return expr;
}

Expr *
dir_expr(Path where)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_PARAM;
	expr->e_level = 0;
	expr->e_where = p_stash(p_reverse(where));
	return expr;
}

Expr *
pair_expr(Expr *left, Expr *right)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_PAIR;
	expr->e_left = left;
	expr->e_right = right;
	return expr;
}

Expr *
apply_expr(Expr *func, Expr *arg)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_APPLY;
	expr->e_func = func;
	expr->e_arg = arg;
	return expr;
}

Expr *
func_expr(Branch *branches)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_LAMBDA;
	expr->e_branch = branches;
	expr->e_arity = 0;
	/* use the first branch for the arity: checked later in nv_expr() */
	for (auto formals = branches->br_formals;
	     formals != nullptr && formals->e_class == expr_type::E_APPLY;
	     formals = formals->e_func)
		expr->e_arity++;
	return expr;
}

/*
 *	Representation of various other structures.
 */

Expr *
ite_expr(Expr *if_expr, Expr *then_expr, Expr *else_expr)
{
	auto expr = apply_expr(apply_expr(apply_expr(
					id_expr(newstring("if_then_else")),
					if_expr),
				then_expr),
			else_expr);
	expr->e_class = expr_type::E_IF;
	return expr;
}

Expr *
let_expr(Expr *pattern, Expr *body, Expr *subexpr, Bool recursive)
{
	auto expr = apply_expr(func_expr(
				new_unary(pattern, subexpr, nullptr)),
			body);
	expr->e_class = recursive ? expr_type::E_RLET : expr_type::E_LET;
	expr->e_func->e_class = expr_type::E_EQN;
	return expr;
}

Expr *
where_expr(Expr *subexpr, Expr *pattern, Expr *body, Bool recursive)
{
	auto expr = apply_expr(func_expr(
				new_unary(pattern, subexpr, nullptr)),
			body);
	expr->e_class = recursive ? expr_type::E_RWHERE : expr_type::E_WHERE;
	expr->e_func->e_class = expr_type::E_EQN;
	return expr;
}

Expr *
mu_expr(Expr *muvar, Expr *body)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_MU;
	expr->e_muvar = apply_expr(nullptr, muvar);
	expr->e_body = body;
	return expr;
}

Expr *
presection(String operator_, Expr *arg)
{
	auto expr = func_expr(new_unary(
			id_expr(bound_variable),
			apply_expr(id_expr(operator_),
				pair_expr(arg, id_expr(bound_variable))),
			nullptr));
	expr->e_class = expr_type::E_PRESECT;
	return expr;
}

Expr *
postsection(String operator_, Expr *arg)
{
	auto expr = func_expr(new_unary(
			id_expr(bound_variable),
			apply_expr(id_expr(operator_),
				pair_expr(id_expr(bound_variable), arg)),
			nullptr));
	expr->e_class = expr_type::E_POSTSECT;
	return expr;
}

/*
 *	Kinds of expression used to represent built-in functions.
 */

Expr *
builtin_expr(Function *fn)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_BUILTIN;
	expr->e_fn = fn;
	return expr;
}

Expr *
bu_1math_expr(Unary *fn)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_BU_1MATH;
	expr->e_1math = fn;
	return expr;
}

Expr *
bu_2math_expr(Binary *fn)
{
	auto expr = NEW(Expr);
	expr->e_class = expr_type::E_BU_2MATH;
	expr->e_2math = fn;
	return expr;
}

/*
 *	Branches of lambdas or defined functions.
 */

Branch *
new_branch(Expr *formals, Expr *expr, Branch *next)
{
	auto branch = NEW(Branch);
	branch->br_formals = formals;
	branch->br_expr = expr;
	branch->br_next = next;
	return branch;
}

Branch *
new_unary(Expr *pattern, Expr *expr, Branch *next)
{
	return new_branch(apply_expr(nullptr, pattern), expr, next);
}

/*
 *	Defined value names
 */

void
decl_value(String name, QType *qtype)
{
	Func	*fn;
	Cons	*cp;

	if (erroneous)
		return;
	if (((fn = fn_local(name)) != nullptr && fn->f_explicit_dec) ||
	    ((cp = cons_local(name)) != nullptr && cp != succ))
		error(SEMERR, "'%s': value identifier already declared", name);
	else {
		if (fn != nullptr)
			del_fn(fn);
		new_fn(name, qtype);
		preserve();
	}
}

void
def_value(Expr *formals, Expr *body)
{
	Func	*fn;
	Branch	*br;
	int	arity;
	Expr	*head;

	if (erroneous)
		return;

	/* special treatment of if-then-else */
	if (formals->e_class == expr_type::E_IF)
		formals->e_class = expr_type::E_APPLY;

	arity = 0;
	for (head = formals; head->e_class == expr_type::E_APPLY; head = head->e_func)
		arity++;

	if (head->e_class != expr_type::E_VAR)
		error(SEMERR, "illegal left-hand-side");
	else if ((fn = fn_local(head->e_vname)) == nullptr)
		error(SEMERR, "'%s': value identifier not locally declared",
			head->e_vname);
	else if (fn->f_explicit_def && fn->f_arity != arity)
		error(SEMERR,
			"'%s': attempted redefinition with a different arity",
			head->e_vname);
	else if (fn->f_code != nullptr && arity == 0)
		error(SEMERR, "'%s': attempt to redefine value identifier",
			head->e_vname);
	else {	
		auto branch = new_branch(formals, body, (Branch *)0);
		/* BUG: implicitly declared functions are not checked */
		if (! nr_branch(branch) ||
		    (fn->f_explicit_dec && ! chk_func(branch, fn)))
			return;		/* some error reported */
		if (! fn->f_explicit_def) {
			fn->f_code = nullptr;
			fn->f_branch = nullptr;
			fn->f_explicit_def = TRUE;
		}
		head->e_class = expr_type::E_DEFUN;
		head->e_defun = fn;
		fn->f_arity = arity;
		/* add the branch at the end */
		if (fn->f_branch == nullptr)
			fn->f_branch = branch;
		else {
			for (br = fn->f_branch;
			     br->br_next != nullptr;
			     br = br->br_next)
				;
			br->br_next = branch;
		}
		/* compile it */
		if (fn->f_code == nullptr && arity > 0)
			fn->f_code = f_nomatch(fn);
		fn->f_code = comp_branch(fn->f_code, branch);
		preserve();
	}
}

static Expr	*textlist(const char *const *sp);

void
init_argv(void)
{
	def_value(id_expr(newstring("argv")), textlist(cmd_args));
}

static Expr *
textlist(const char *const *sp)
{
	return *sp == nullptr ? e_nil :
		apply_expr(e_cons,
			pair_expr(text_expr((const Byte *)*sp, strlen(*sp)),
			textlist(sp+1)));
}
