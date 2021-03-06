#include "defs.h"
#include "pr_type.h"
#include "deftype.h"
#include "cons.h"
#include "polarity.h"
#include "op.h"
#include "print.h"
#include "names.h"

/*
 *	Printing of types.
 */

static void	pr_tycons(FILE *f, DefType *dt, TypeList *args);
static void	ty_print(FILE *f, Type *type, int context);
static int	ty_precedence(Type *type);
static void	pr_alt(FILE *f, Cons *alt);

void
pr_qtype(FILE *f, QType *qtype)
{
	pr_type(f, qtype->qt_type);
}

void
pr_type(FILE *f, Type *type)
{
	ty_print(f, type, PREC_BODY);
}

static void
ty_print(FILE *f, Type *type, int context)
{
	auto prec = ty_precedence(type);
	if (prec < context)
		(void)fprintf(f, "(");
	switch (type->ty_class) {
	case TY_VAR:
		(void)fprintf(f, "%s", type->ty_var);
        break;
    case TY_MU:
		(void)fprintf(f, "%s %s %s ", n_mu, type->ty_muvar, n_gives);
		ty_print(f, type->ty_body, prec);
        break;
    case TY_CONS:
		pr_tycons(f, type->ty_deftype, type->ty_args);
        break;
    default:
		NOT_REACHED;
	}
	if (prec < context)
		(void)fprintf(f, ")");
}

static void
pr_tycons(FILE *f, DefType *dt, TypeList *args)
{
	Op	*op;

	if (! dt->dt_tupled) {
		(void)fprintf(f, "%s", dt->dt_name);
		for ( ; args != nullptr; args = args->ty_tail) {
			(void)fprintf(f, " ");
			ty_print(f, args->ty_head, PREC_ARG);
		}
	} else if (dt->dt_arity == 2 && (op = op_lookup(dt->dt_name)) != nullptr) {
		ty_print(f, args->ty_head, LeftPrec(op));
		(void)fprintf(f, " %s ", dt->dt_name);
		ty_print(f, args->ty_tail->ty_head, RightPrec(op));
	} else {
		(void)fprintf(f, "%s (", dt->dt_name);
		for ( ; args != nullptr; args = args->ty_tail) {
			ty_print(f, args->ty_head, PREC_BODY);
			if (args->ty_tail != nullptr)
				(void)fprintf(f, ", ");
		}
		(void)fprintf(f, ")");
	}
}

static int
ty_precedence(Type *type)
{
	Op	*op;

	if (type->ty_class == TY_VAR || type->ty_deftype->dt_arity == 0)
		return PREC_ATOMIC;
	if (type->ty_class == TY_MU)
		return PREC_MU;
	if (type->ty_deftype->dt_arity == 2 &&
	    (op = op_lookup(type->ty_deftype->dt_name)) != nullptr)
		return op->op_prec;
	return PREC_APPLY;
}

void
pr_deftype(FILE *f, DefType *dt, Bool full)
{
	TypeList *argp;
	Cons	*alt;

	(void)fprintf(f, "%s ",
		full && IsSynType(dt) ? n_type :
		full && IsDataType(dt) ? n_data : n_abstype);
	if (dt->dt_arity == 2 && dt->dt_tupled &&
		 op_lookup(dt->dt_name) != nullptr)
		(void)fprintf(f, "%s %s %s",
			type_arg_name(dt->dt_varlist->ty_head, full),
			dt->dt_name,
			type_arg_name(dt->dt_varlist->ty_tail->ty_head, full));
	else if (dt->dt_tupled) {
		(void)fprintf(f, "%s(", dt->dt_name);
		for (argp = dt->dt_varlist; argp != nullptr; argp = argp->ty_tail)
			(void)fprintf(f, argp->ty_tail == nullptr ? "%s" : "%s, ",
				type_arg_name(argp->ty_head, full));
		(void)fprintf(f, ")");
	} else {
		(void)fprintf(f, "%s", dt->dt_name);
		for (argp = dt->dt_varlist; argp != nullptr; argp = argp->ty_tail)
			(void)fprintf(f, " %s",
				type_arg_name(argp->ty_head, full));
	}
	if (full) {
		if (IsSynType(dt)) {
			(void)fprintf(f, " %s ", n_eq);
			pr_type(f, dt->dt_type);
		} else if (IsDataType(dt)) {
			(void)fprintf(f, " %s ", n_eq);
			for (alt = dt->dt_cons;
			     alt != nullptr;
			     alt = alt->c_next) {
				pr_alt(f, alt);
				if (alt->c_next != nullptr)
					(void)fprintf(f, " %s ", n_or);
			}
		}
	}
	(void)fprintf(f, ";\n");
}

static void
pr_alt(FILE *f, Cons *alt)
{
	Op	*op;
	TypeList *args;

	if (alt->c_nargs == 0)
		(void)fprintf(f, "%s", alt->c_name);
	if (alt->c_nargs == 1 && (op = op_lookup(alt->c_name)) != nullptr) {
		ty_print(f, alt->c_type->ty_firstarg->ty_firstarg,
			LeftPrec(op));
		(void)fprintf(f, " %s ", alt->c_name);
		ty_print(f, alt->c_type->ty_firstarg->ty_secondarg,
			RightPrec(op));
	} else {
		(void)fprintf(f, "%s", alt->c_name);
		for (args = alt->c_type->ty_args;
		     args != nullptr;
		     args = args->ty_tail) {
			(void)fprintf(f, " ");
			ty_print(f, args->ty_head, PREC_ARG);
		}
	}
}
