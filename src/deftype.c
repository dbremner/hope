#include "defs.h"
#include "deftype.h"
#include "cons.h"
#include "memory.h"
#include "bad_rectype.h"
#include "functors.h"
#include "expr.h"
#include "polarity.h"
#include "type_check.h"
#include "error.h"

/*
 *	Defined Types.
 */

/* Internal names of some types and constructors. */
DefType *product, *function, *list, *num, *truval, *character;
Cons	*nil, *cons, *succ, *true_, *false_;

/*
 *	The type currently being defined.
 *	(not yet recorded in the tables, in case of error,
 *	except in the case of an abstract type being defined)
 */
static	DefType	*cur_deftype;		/* type currently being defined */
static	TypeList *cur_varlist;		/* its new formal parameters */
static Type	*cur_newtype;		/* application of the above */
static Bool	already_defined;	/* current type is already defined */

static String	mu_names[MAX_MU_DEPTH];	/* stack of mu type variables */
static String	*mu_top;

static Type	*current_newtype(void);
static Bool	args_repeated(TypeList *varlist);
static Bool	is_header(Type *type, DefType *deftype);
static int	ty_length(TypeList *typelist);

static int	nr_type(Type *type);
static void	nv_type(Type *type);
static int	nv_tvar(String name);

static Type	*multi_pair_type(TypeList *args);
static Type	*multi_func_type(TypeList *args, Type *result);

void
start_dec_type()
{
	cur_deftype = nullptr;
	mu_top = mu_names;
}

DefType *
new_deftype(String name, Bool tupled, TypeList *vars)
{
	TypeList *varp;
	int	i;

	auto dt = dt_local(name);
	auto arity = ty_length(vars);
	already_defined = dt != nullptr;
	if (already_defined) {
		if (! IsAbsType(dt)) {
			error(SEMERR, "'%s': attempt to redefine type", name);
			return dt;
		}
		if (arity != dt->dt_arity) {
			error(SEMERR,
				"'%s': wrong number of type arguments",
				name);
			return dt;
		}
		if (tupled != dt->dt_tupled) {
			error(SEMERR,
				"'%s': different argument syntax", name);
			return dt;
		}
	}
	else {
		if (arity > MAX_TVARS_IN_TYPE)
			error(SEMERR, "'%s': too many type arguments", name);
		dt = NEW(DefType);
		dt->dt_name = name;
		dt->dt_syn_depth = 0;
		dt->dt_index = 0;
		dt->dt_tupled = tupled;
		dt->dt_cons = nullptr;
		dt->dt_arity = arity;
		dt->dt_varlist = nullptr;
	}

	for (varp = vars, i = 0; varp != nullptr; varp = varp->ty_tail, i++)
		varp->ty_head->ty_index = i;

	cur_varlist = vars;
	cur_deftype = dt;
	cur_newtype = nullptr;
	mu_top = mu_names;
	return dt;
}

static Type *
current_newtype(void)
{
	ASSERT( cur_deftype != nullptr );
	if (cur_newtype == nullptr)
		cur_newtype = def_type(cur_deftype, cur_varlist);
	return cur_newtype;
}

void
abstype(DefType *deftype)
{
	if (erroneous || already_defined)
		return;

	/* definition is OK -- add it to the table */
	set_polarities(cur_varlist);
	deftype->dt_varlist = cur_varlist;
	dt_declare(deftype);
	preserve();
}

void
type_syn(DefType *deftype, Type *type)
{
	if (erroneous || args_repeated(cur_varlist))
		return;

	if (is_header(type, deftype)) {
		error(SEMERR, "'%s': left-recursive type definition",
			deftype->dt_name);
		return;
	}

	if (bad_rectype(deftype, type))
		return;

	start_polarities(deftype, cur_varlist);
	compute_polarities(type);
	finish_polarities();

	if (already_defined &&
	    ! check_polarities(deftype->dt_varlist, cur_varlist))
		return;

	/* definition is OK -- add it to the table */
	deftype->dt_varlist = cur_varlist;
	deftype->dt_type = type;
	while (type->ty_class == TY_MU)
		type = type->ty_body;
	ASSERT( type->ty_class == TY_VAR || type->ty_class == TY_CONS );
	deftype->dt_syn_depth = 1 +
		(type->ty_class == TY_VAR ? 0 :
			type->ty_deftype->dt_syn_depth);
	if (deftype->dt_syn_depth > MAX_SYN_DEPTH)
		error(SEMERR, "type synonyms nested too deeply");

	if (already_defined)
		fix_synonyms();
	else
		dt_declare(deftype);
	def_functor(deftype);
	preserve();
}

/*
 *	Is the expansion of type headed by deftype?
 */
static Bool
is_header(Type *type, DefType *deftype)
{
	for (;;) {
		switch (type->ty_class) {
		case TY_VAR:
			return FALSE;
		case TY_MU:
			type = type->ty_body;
            break;
        case TY_CONS:
			if (type->ty_deftype == deftype)
				return TRUE;
			if (! IsSynType(type->ty_deftype))
				return FALSE;
			type = type->ty_deftype->dt_type;
            break;
        default:
			NOT_REACHED;
		}
	}
}

void
decl_type(DefType *deftype, Cons *conslist)
{
	Cons	*ct;
	Type	*type;
	Natural	c_index;
	Func	*fn;

	if (erroneous || args_repeated(cur_varlist))
		return;

	start_polarities(deftype, cur_varlist);
	c_index = 0;
	for (ct = conslist; ct != nullptr; ct = ct->c_next) {
		if (cons_local(ct->c_name) != nullptr) {
			error(SEMERR, "'%s': attempt to redefine constructor",
				ct->c_name);
			return;
		}

		ct->c_nargs = 0;
		for (type = ct->c_type;
		     type->ty_deftype == function;
		     type = type->ty_secondarg) {
			if (bad_rectype(deftype, type->ty_firstarg))
				return;
			compute_polarities(type->ty_firstarg);
			ct->c_nargs++;
		}

		ct->c_index = c_index++;
		ct->c_ntvars = deftype->dt_arity;

		if ((fn = fn_local(ct->c_name)) != nullptr) {
			/* fulfilling a declaration */
			if (fn->f_code != nullptr) {
				error(SEMERR,
					"'%s': attempt to redefine value identifier",
					ct->c_name);
				return;
			}
			/* BUG: implicitly defined fns not checked */
			if (! fn->f_explicit_dec &&
			    ! ty_instance(fn->f_type, fn->f_ntvars,
					ct->c_type, ct->c_ntvars)) {
				error(SEMERR,
					"'%s': type does not match declaration",
					ct->c_name);
				return;
			}
		}
	}
	finish_polarities();

	if (already_defined &&
	    ! check_polarities(deftype->dt_varlist, cur_varlist))
		return;

	/* definition is OK -- add it to the table */
	deftype->dt_varlist = cur_varlist;
	deftype->dt_cons = conslist;

	for (ct = conslist; ct != nullptr; ct = ct->c_next)
		if ((fn = fn_local(ct->c_name)) != nullptr) {
			def_value(id_expr(fn->f_name), cons_expr(ct));
			fn->f_explicit_def = FALSE;
		}
	if (! already_defined)
		dt_declare(deftype);
	def_functor(deftype);
	preserve();
}

/*
 *	Check a list of type variables for repetitions.
 */
static Bool
args_repeated(TypeList *varlist)
{
	TypeList *vp;

	for ( ; varlist != nullptr; varlist = varlist->ty_tail)
		for (vp = varlist->ty_tail; vp != nullptr; vp = vp->ty_tail)
			if (vp->ty_head->ty_var == varlist->ty_head->ty_var) {
				error(SEMERR, "'%s': parameter is repeated",
					vp->ty_head->ty_var);
				return TRUE;
			}
	return FALSE;
}

/*
 *	Lists of data constructors.
 */

Cons *
constructor(String name, Bool tupled, TypeList *args)
{
	auto c = NEW(Cons);
	c->c_name = name;
	c->c_type = tupled ?
			func_type(multi_pair_type(args), current_newtype()) :
			multi_func_type(args, current_newtype());
	c->c_next = nullptr;
	return c;
}

Cons *
alt_cons(Cons *constr, Cons *next)
{
	constr->c_next = next;
	return constr;
}

/*
 *	Type structures.
 */

static int
ty_length(TypeList *typelist)
{
	int	len;

	len = 0;
	for ( ; typelist != nullptr; typelist = typelist->ty_tail)
		len++;
	return len;
}

/*
 *	Type constructor application.
 *	If tupled is TRUE, there must be at least 2 arguments.
 */
Type *
new_type(String name, Bool tupled, TypeList *args)
{
	Type	*type;
	DefType	*deftype;
	TypeList *tparam;
	String	*mu_ptr;

	if (args == nullptr) { /* nullary constructor: may be type variable */
		/* is it bound by a mu quantifier? */
		for (mu_ptr = mu_top-1; mu_ptr >= mu_names; mu_ptr--)
			if (*mu_ptr == name) {
				type = new_tv(name);
				type->ty_mu_bound = TRUE;
				type->ty_index = mu_top - mu_ptr - 1;
				return type;
			}
		/* in a type definition, must be a parameter */
		if (cur_deftype != nullptr) {
			for (tparam = cur_varlist;
			     tparam != nullptr;
			     tparam = tparam->ty_tail)
				if (name == tparam->ty_head->ty_var)
					return tparam->ty_head;
		/* in a value declaration, must be a declared type var */
		} else if (tv_lookup(name))
			return new_tv(name);
	}

	if (cur_deftype != nullptr && name == cur_deftype->dt_name)
		deftype = cur_deftype;
	else if ((deftype = dt_lookup(name)) == nullptr) {
		error(SEMERR, "'%s' is not a defined type", name);
		deftype = truval;	/* dummy */
	}
	if (deftype->dt_arity != ty_length(args))
		error(SEMERR,
			"'%s': wrong number of type arguments", name);
	else if (deftype->dt_tupled != tupled)
		error(SEMERR, "'%s': different argument syntax", name);

	type = NEW(Type);
	type->ty_class = TY_CONS;
	type->ty_tupled = tupled;
	type->ty_deftype = deftype;
	type->ty_args = args;
	return type;
}

Type *
def_type(DefType *dt, TypeList *args)
{
	auto type = NEW(Type);
	type->ty_class = TY_CONS;
	type->ty_deftype = dt;
	type->ty_args = args;
	return type;
}

/*
 *	Enter a mu scope.
 */
void
enter_mu_tv(String name)
{
	*mu_top++ = name;
}

/*
 *	Leave a mu scope, building the mu type.
 */
Type *
mu_type(Type *body)
{
	auto type = NEW(Type);
	type->ty_class = TY_MU;
	type->ty_muvar = *--mu_top;
	type->ty_body = body;
	return type;
}

Type *
new_tv(TVar tvar)
{
	auto type = NEW(Type);
	type->ty_class = TY_VAR;
	type->ty_mu_bound = FALSE;
	type->ty_var = tvar;
	return type;
}

TypeList *
cons_type(Type *type, TypeList *typelist)
{
	if (type == nullptr)
		return typelist;
	auto tl = NEW(TypeList);
	tl->ty_head = type;
	tl->ty_tail = typelist;
	return tl;
}

Type *
pair_type(Type *type1, Type *type2)
{
	return def_type(product,
		cons_type(type1, cons_type(type2, nullptr)));
}

Type *
func_type(Type *type1, Type *type2)
{
	return def_type(function,
		cons_type(type1, cons_type(type2, nullptr)));
}

static Type *
multi_pair_type(TypeList *args)
{
	return args->ty_tail == nullptr ? args->ty_head :
		pair_type(args->ty_head, multi_pair_type(args->ty_tail));
}

static Type *
multi_func_type(TypeList *args, Type *result)
{
	return args == nullptr ? result :
		func_type(args->ty_head,
			multi_func_type(args->ty_tail, result));
}

/*
 *	Qualified types
 */

QType *
qualified_type(Type *type)
{
	auto qtype = NEW(QType);
	qtype->qt_type = type;
	qtype->qt_ntvars = nr_type(type);
	if (qtype->qt_ntvars > MAX_TVARS_IN_TYPE)
		error(SEMERR, "too many type variables");
	return qtype;
}

/*
 *	Numbering of type variables.
 */

static String	*base_vars_seen;
static String	*last_vars_seen;

static int
nr_type(Type *type)
{
	String	vars_seen[MAX_TVARS_IN_TYPE];

	last_vars_seen = base_vars_seen = vars_seen;
	nv_type(type);
	return last_vars_seen - vars_seen;
}

static void
nv_type(Type *type)
{
	switch (type->ty_class) {
	case TY_VAR:
		if (! type->ty_mu_bound)
			type->ty_index = nv_tvar(type->ty_var);
        break;
    case TY_MU:
		nv_type(type->ty_body);
        break;
    case TY_CONS:
		for (auto argp = type->ty_args; argp != nullptr; argp = argp->ty_tail)
			nv_type(argp->ty_head);
        break;
    default:
		NOT_REACHED;
	}
}

static int
nv_tvar(String name)
{
	String	*varp;

	*last_vars_seen = name;		/* also serves as a sentinel */
	for (varp = base_vars_seen; *varp != name; varp++)
		;
	if (varp == last_vars_seen)
		last_vars_seen++;
	return varp - base_vars_seen;
}
