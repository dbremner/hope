#include "defs.h"
#include "type_check.h"
#include "expr.h"
#include "deftype.h"
#include "cons.h"
#include "pr_expr.h"
#include "pr_type.h"
#include "type_value.h"
#include "pr_ty_value.h"
#include "functor_type.h"
#include "op.h"
#include "error.h"
#include "exceptions.h"

Cell	*expr_type;	/* last inferred type */

	/* Types of local program variables, allocated with new_vars() */
static Cell	**next_vtype;
	/* Types of variables local to a pattern or parameter */
static Cell	**local_var;
	/* Local variables at each level */
static Cell	***variables;

static void	match_type(String name, Cell *inferred, QType *declared);

static Cell	*ty_expr(Expr *expr);
static Cell	*ty_pattern(Expr *pattern, int level);
static Cell	*ty_if(Expr *expr);
static Cell	*ty_eqn(Branch *branch, Expr *expr);
static Cell	*ty_rec_eqn(Branch *branch, Expr *expr);
static Cell	*ty_mu_expr(Expr *muvar, Expr *body);
static Cell	*ty_list(Branch *branch);
static Cell	*ty_branch(Branch *branch);
static Cell	*ty_formals(Expr *formals, Cell *type);

static DefType	*get_functor(Expr *expr);

static void	init_vars(void);
static void	new_vars(int n);
static void	del_vars(void);

static void	show_argument(Expr *func, Expr *arg, Cell *arg_type);
static void	show_expr_type(Expr *expr, Cell *type);
static void	show_expr(Expr *expr);
static void	show_branch(Branch *branch);

Bool
chk_func(Branch *branch, Func *fn)
{
	if (setjmp(execerror))
		return FALSE;
	init_vars();
	auto inferred = ty_branch(branch);
	match_type(fn->f_name, inferred, fn->f_qtype);
	return TRUE;
}

/*
 *	Check that the inferred type is compatible with (at least as
 *	general as) the declared type.
 */
static void
match_type(String name, Cell *inferred, QType *declared)
{
	if (! instance(declared->qt_type, declared->qt_ntvars, inferred)) {
		start_err_line();
		(void)fprintf(errout, "  declared type: ");
		pr_qtype(errout, declared);
		(void)fprintf(errout, "\n");
		start_err_line();
		(void)fprintf(errout, "  inferred type: ");
		pr_ty_value(errout, inferred);
		(void)fprintf(errout, "\n");
		error(TYPEERR, "'%s': does not match declaration", name);
	}
}

Bool
ty_instance(Type *type1, Natural ntvars1, Type *type2, Natural ntvars2)
{
	if (setjmp(execerror))
		return FALSE;
	init_vars();
	return instance(type1, ntvars1, copy_type(type2, ntvars2, FALSE));
}

/*
 *	Top level: must have
 *		lambda input => expr: list char -> T
 *
 *	Side effect: set expr_type to T.
 */
void
chk_expr(Expr *expr)
{
	init_vars();
	new_vars(0);
	*next_vtype++ = new_list_type(new_const_type(character));
	expr_type = ty_expr(expr);
	del_vars();
}

void
chk_list(Expr *expr)
{
	chk_expr(expr);
	if (! unify(expr_type, new_list_type(new_tvar()))) {
		show_expr_type(expr, expr_type);
		error(TYPEERR, "a 'write' expression must produce a list");
	}
}

static Cell *
ty_expr(Expr *expr)
{
	Cell	*type1, *type2;
	DefType	*dt;

	switch (expr->e_class) {
	case expr_type::E_NUM:
		return new_const_type(num);
	case expr_type::E_CHAR:
		return new_const_type(character);
	case expr_type::E_DEFUN:
		if ((dt = get_functor(expr)) != nullptr)
			return functor_type(dt);
		return copy_type(expr->e_defun->f_type,
				expr->e_defun->f_ntvars, FALSE);
	case expr_type::E_CONS:
		/*
		 * Restricted types of list and string syntax:
		 */
		if (expr == e_nil)	/* [] : list alpha */
			return new_list_type(new_tvar());
		if (expr == e_cons) {
			/* alpha # list alpha -> list alpha */
			type1 = new_tvar();
			type2 = new_list_type(type1);
			return new_func_type(
					new_prod_type(type1, type2),
					type2);
		}
		return copy_type(expr->e_const->c_type,
				expr->e_const->c_ntvars, FALSE);
    case expr_type::E_LAMBDA:
    case expr_type::E_PRESECT:
    case expr_type::E_POSTSECT:
		return ty_list(expr->e_branch);
	case expr_type::E_PARAM:
		if ((dt = get_functor(expr)) != nullptr)
			return functor_type(dt);
		return ty_pattern(expr->e_patt, expr->e_level);
	case expr_type::E_PLUS:
		type1 = new_const_type(num);
		type2 = ty_expr(expr->e_rest);
		if (! unify(type1, type2)) {
			show_expr(expr);
			show_expr_type(expr->e_rest, type2);
			error(TYPEERR, "argument has wrong type");
		}
		return type1;
	case expr_type::E_VAR:
		/*
		 *	... , x: t, ... |- x: t
		 */
		return local_var[(int)expr->e_var];
	case expr_type::E_PAIR:
		/*
		 *	A |- e1: t1
		 *	A |- e2: t2
		 *	-------------
		 *	A |- e1, e1: t1 # t2
		 */
		return new_prod_type(ty_expr(expr->e_left),
				     ty_expr(expr->e_right));
	case expr_type::E_IF:
		return ty_if(expr);
    case expr_type::E_WHERE:
    case expr_type::E_LET:
		return ty_eqn(expr->e_func->e_branch, expr->e_arg);
    case expr_type::E_RWHERE:
    case expr_type::E_RLET:
		return ty_rec_eqn(expr->e_func->e_branch, expr->e_arg);
	case expr_type::E_MU:
		return ty_mu_expr(expr->e_muvar, expr->e_body);
	case expr_type::E_APPLY:
		/*
		 *	A |- e1: t2 -> t
		 *	A |- e2: t2
		 *	-------------
		 *	A |- (e1 e2): t
		 */
		type1 = ty_expr(expr->e_func);
		type2 = ty_expr(expr->e_arg);
		if (! unify(type1, new_func_type(type2, new_tvar()))) {
			show_expr(expr);
			show_expr_type(expr->e_func, type1);
			show_argument(expr->e_func, expr->e_arg, type2);
			error(TYPEERR, "argument has wrong type");
		}
		return deref(type1)->c_targ2;
    case expr_type::E_EQN:
    case expr_type::E_BU_1MATH:
    case expr_type::E_BU_2MATH:
    case expr_type::E_BUILTIN:
    case expr_type::E_RETURN:
    case expr_type::E_NCLASSES:
		NOT_REACHED;
	}
}

static Cell *
ty_pattern(Expr *pattern, int level)
{
	local_var = variables[level];
	return ty_expr(pattern);
}

static Cell *
ty_if(Expr *expr)
{
	auto if_expr = expr->e_func->e_func->e_arg;
	auto then_expr = expr->e_func->e_arg;
	auto else_expr = expr->e_arg;
	auto type1 = ty_expr(if_expr);
	if (! unify(type1, new_const_type(truval))) {
		show_expr_type(if_expr, type1);
		error(TYPEERR, "predicate is not a truth value");
	}

	type1 = ty_expr(then_expr);
	auto type2 = ty_expr(else_expr);
	if (! unify(type1, type2)) {
		show_expr_type(then_expr, type1);
		show_expr_type(else_expr, type2);
		error(TYPEERR, "conflict between branches of conditional");
	}
	return type1;
}

/*
 *	A' |- pat: t1
 *	A, A' |- val: t2
 *	A |- exp: t1
 *	--------------------
 *	A |- LET pat == exp IN val: t2
 */
static Cell *
ty_eqn(Branch *branch, Expr *expr)
{
	new_vars(branch->br_formals->e_nvars);
	auto pat_type = ty_pattern(branch->br_formals->e_arg, 0);
	auto val_type = ty_expr(branch->br_expr);
	del_vars();
	auto exp_type = ty_expr(expr);
	if (! unify(pat_type, exp_type)) {
		show_expr_type(branch->br_formals->e_arg, pat_type);
		show_expr_type(expr, exp_type);
		error(TYPEERR, "sides of equation have conflicting types");
	}
	return val_type;
}

/*
 *	A' |- pat: t1
 *	A, A' |- val: t2
 *	A, A' |- exp: t1
 *	--------------------
 *	A |- LETREC pat == exp IN val: t2
 */
static Cell *
ty_rec_eqn(Branch *branch, Expr *expr)
{
	new_vars(branch->br_formals->e_nvars);
	auto pat_type = ty_pattern(branch->br_formals->e_arg, 0);
	auto val_type = ty_expr(branch->br_expr);
	auto exp_type = ty_expr(expr);
	del_vars();
	if (! unify(pat_type, exp_type)) {
		show_expr_type(branch->br_formals->e_arg, pat_type);
		show_expr_type(expr, exp_type);
		error(TYPEERR, "sides of equation have conflicting types");
	}
	return val_type;
}

/*
 *	A' |- pat: t
 *	A, A' |- exp: t
 *	--------------------
 *	A |- MU pat => exp : t
 */
static Cell *
ty_mu_expr(Expr *muvar, Expr *body)
{
	new_vars(muvar->e_nvars);
	auto pat_type = ty_pattern(muvar->e_arg, 0);
	auto exp_type = ty_expr(body);
	del_vars();
	if (! unify(pat_type, exp_type)) {
		show_expr_type(muvar->e_arg, pat_type);
		show_expr_type(body, exp_type);
		error(TYPEERR, "pattern and body have conflicting types");
	}
	return exp_type;
}

/*
 *	A |- b1: t
 *	...
 *	A |- bn: t
 *	--------------
 *	A |- (lambda b1 | ... | bn): t
 */
static Cell *
ty_list(Branch *branch)
{
	auto type = ty_branch(branch);
	for (auto br = branch->br_next; br != nullptr; br = br->br_next)
		if (! unify(type, ty_branch(br))) {
			show_branch(branch);
			error(TYPEERR, "alternatives have incompatible types");
		}
	return type;
}

/*
 *	A1 |- p1: t1
 *	...
 *	An |- pn: tn
 *	A, A1, ..., An |- e: t
 *	-----------------
 *	A |- (p1 ... pn => e): t1 -> ... -> tn -> t
 *
 * Because pn is at the front of the list, and we want to check e after
 * checking the p's, a rather messy recursion is required.
 */
static Cell *
ty_branch(Branch *branch)
{
	Cell	**tp;
	auto type = ty_formals(branch->br_formals, NOCELL);
	/* plug the result type */
	for (tp = &type; *tp != NOCELL; tp = &((*tp)->c_targ2))
		;
	*tp = ty_expr(branch->br_expr);
	/* delete all the variables pushed by ty_formals() */
	for (auto formals = branch->br_formals;
	     formals != nullptr && formals->e_class == expr_type::E_APPLY;
	     formals = formals->e_func)
		del_vars();
	return type;
}

static Cell *
ty_formals(Expr *formals, Cell *type)
{
	if (formals == nullptr || formals->e_class != expr_type::E_APPLY)
		return type;
	auto newtype = new_func_type(NOCELL, type);
	type = ty_formals(formals->e_func, newtype);
	new_vars(formals->e_nvars);
	newtype->c_targ1 = ty_pattern(formals->e_arg, 0);
	return type;
}

static DefType *
get_functor(Expr *expr)
{
	switch (expr->e_class) {
	case expr_type::E_DEFUN:
		if (! expr->e_defun->f_explicit_dec)
			return expr->e_defun->f_tycons;
		else
			return nullptr;
    case expr_type::E_BU_1MATH:
    case expr_type::E_BU_2MATH:
    case expr_type::E_BUILTIN:
    case expr_type::E_RETURN:
    case expr_type::E_NCLASSES:
    case expr_type::E_PLUS:
    case expr_type::E_VAR:
    case expr_type::E_IF:
    case expr_type::E_MU:
    case expr_type::E_LET:
    case expr_type::E_NUM:
    case expr_type::E_CHAR:
    case expr_type::E_PARAM:
    case expr_type::E_PAIR:
    case expr_type::E_RLET:
    case expr_type::E_APPLY:
    case expr_type::E_WHERE:
    case expr_type::E_RWHERE:
    case expr_type::E_EQN:
    case expr_type::E_LAMBDA:
    case expr_type::E_PRESECT:
    case expr_type::E_POSTSECT:
    case expr_type::E_CONS:
		return nullptr;
	}
}

/*
 *	Type variable scopes.
 */

static void
init_vars(void)
{
static	Cell	*first_vtype[MAX_VARIABLES];
static	Cell	**local_table[MAX_SCOPES];

	start_heap();
	next_vtype = first_vtype;
	variables = local_table + MAX_SCOPES;
	init_pr_ty_value();
}

/*
 *	New scope: allocate and initialize a new type variable
 *	for each program variable introduced in the branch.
 */
static void
new_vars(int n)
{
	*--variables = next_vtype;
	/*
	 *	x1: alpha1, ..., xn: alphan
	 */
	while (n-- > 0)
		*next_vtype++ = new_tvar();
}

static void
del_vars(void)
{
	next_vtype = *variables++;
}

/*
 *	Display various things and their types,
 *	to enlighten the user about a type error.
 */

static void
show_argument(Expr *func, Expr *arg, Cell *arg_type)
{
	String	name;

	if (arg->e_class == expr_type::E_PAIR &&
	    (name = expr_name(func, MAX_SCOPES)) != nullptr &&
	    op_lookup(name) != nullptr) {
		arg_type = deref(arg_type);
		show_expr_type(arg->e_left, arg_type->c_targ1);
		show_expr_type(arg->e_right, arg_type->c_targ2);
	}
	else
		show_expr_type(arg, arg_type);
}

static void
show_expr_type(Expr *expr, Cell *type)
{
	start_err_line();
	(void)fprintf(errout, "  ");
	pr_expr(errout, expr);
	(void)fprintf(errout, " : ");
	pr_ty_value(errout, type);
	(void)fprintf(errout, "\n");
}

static void
show_expr(Expr *expr)
{
	start_err_line();
	(void)fprintf(errout, "  ");
	pr_expr(errout, expr);
	(void)fprintf(errout, "\n");
}

static void
show_branch(Branch *branch)
{
	for ( ; branch != nullptr; branch = branch->br_next) {
		start_err_line();
		(void)fprintf(errout, "  ");
		pr_branch(errout, branch, MAX_SCOPES);
		(void)fprintf(errout, " : ");
		pr_ty_value(errout, ty_branch(branch));
		(void)fprintf(errout, "\n");
	}
}
