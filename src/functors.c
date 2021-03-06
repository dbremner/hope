#include "defs.h"
#include "functors.h"
#include "deftype.h"
#include "cons.h"
#include "expr.h"

/*
 *	Definition of 'functors'.
 */

static Expr	*expr_of_type(Type *type);
static Expr	*expr_of_typelist(TypeList *typelist);
static Expr	*multi_apply_expr(Expr *func, TypeList *typelist);
static Expr	*pat_of_constr(Cons *cp);
static Expr	*body_of_constr(Cons *cp);

/*
 * Identifier STRINGs, different from each other and any real String.
 * The are 26 of them, so that's the maximum arity of data constructors.
 */
static String	variable[] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
	"n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
};

void
def_functor(DefType *dt)
{
	Cons	*cp;
	Expr	*lhs;

	if (dt->dt_arity == 0) {
		/*
		 * Nullary type T gives the definition
		 *	--- T x <= x;
		 */
		def_value(
			apply_expr(id_expr(dt->dt_name),
				id_expr(variable[0])),
			id_expr(variable[0]));
	} else if (IsSynType(dt)) {
		/*
		 * A type synonym definition
		 *	type T(a1, ..., an) == t;
		 * generates a value definition
		 *	--- T(a1, ..., an) <= t;
		 */
		lhs = dt->dt_tupled ?
			apply_expr(id_expr(dt->dt_name),
				expr_of_typelist(dt->dt_varlist)) :
			multi_apply_expr(id_expr(dt->dt_name), dt->dt_varlist);
		def_value(lhs, expr_of_type(dt->dt_type));
	} else {
		/*
		 * A data type definition
		 *	data T(a1, ..., an) == ... ++ c t1 ... tk ++ ...;
		 * generates value definitions
		 *	--- T(a1, ..., an) (c x1 ... xk) <=
		 *			c (t1 x1) ... (tk xk);
		 * Similarly for T a1 ... an
		 */
		for (cp = dt->dt_cons; cp != nullptr; cp = cp->c_next) {
			lhs = dt->dt_tupled ?
				apply_expr(id_expr(dt->dt_name),
					expr_of_typelist(dt->dt_varlist)) :
				multi_apply_expr(id_expr(dt->dt_name),
					dt->dt_varlist);
			def_value(apply_expr(lhs, pat_of_constr(cp)),
					body_of_constr(cp));
		}
	}
	fn_local(dt->dt_name)->f_explicit_def = FALSE;
}

static Expr *
pat_of_constr(Cons *cp)
{
	int	i;
	auto pat = cons_expr(cp);
	for (i = 0; i < cp->c_nargs; i++)
		pat = apply_expr(pat, id_expr(variable[i]));
	return pat;
}

static Expr *
body_of_constr(Cons *cp)
{
	int	i;
	Type	*type;

	auto body = cons_expr(cp);
	for (i = 0, type = cp->c_type;
	     i < cp->c_nargs;
	     i++, type = type->ty_secondarg)
		body = apply_expr(body,
				apply_expr(expr_of_type(type->ty_firstarg),
					id_expr(variable[i])));
	return body;
}

static Expr *
expr_of_type(Type *type)
{
	return type->ty_class == TY_VAR ?
			id_expr(type->ty_var) :
	       type->ty_class == TY_MU ?
			mu_expr(id_expr(type->ty_var),
				expr_of_type(type->ty_body)) :
	       type->ty_deftype->dt_tupled ?
			apply_expr(id_expr(type->ty_deftype->dt_name),
				   expr_of_typelist(type->ty_args)) :
			multi_apply_expr(id_expr(type->ty_deftype->dt_name),
					 type->ty_args);
}

static Expr *
expr_of_typelist(TypeList *typelist)
{
	return typelist->ty_tail == nullptr ? expr_of_type(typelist->ty_head) :
		pair_expr(expr_of_type(typelist->ty_head),
			  expr_of_typelist(typelist->ty_tail));
}

static Expr *
multi_apply_expr(Expr *func, TypeList *typelist)
{
	while (typelist != nullptr) {
		func = apply_expr(func, expr_of_type(typelist->ty_head));
		typelist = typelist->ty_tail;
	}
	return func;
}
