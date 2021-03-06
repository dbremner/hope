#include "defs.h"
#include "remember_type.h"
#include "deftype.h"
#include "cons.h"
#include "expr.h"
#include "error.h"
#include "newstring.h"

typedef struct {
	const	char	*type_name;
	DefType	**type_ref;
} NoteType;

static NoteType note_type[] = {
#ifdef UTF_LIBS
	{ "\xe2\x86\x92", &function },
	{ "\xc3\x97",	&product },
#else
	{ "->",		&function },
	{ "#",		&product },
#endif
	{ "bool",	&truval },
	{ "num",	&num },
	{ "list",	&list },
	{ "char",	&character },
	{ nullptr, nullptr }
};

typedef struct {
	const	char	*cons_name;
	Cons	**cons_ref;
	Expr	**expr_ref;
} NoteCons;

static NoteCons note_cons[] = {
	{ "nil",	&nil,		&e_nil },
	{ "::",		&cons,		&e_cons },
	{ "succ",	&succ,		nullptr },
	{ nullptr, nullptr, nullptr }
};

/*
 *	Remember this one?
 *	Called whenever a type is defined in the Standard module.
 */

void
remember_type(DefType *dt)
{
	NoteType *ntp;
	NoteCons *ncp;
	Cons	*cp;

	for (ntp = note_type; ntp->type_name != nullptr; ntp++)
		if (dt->dt_name == newstring(ntp->type_name))
			*(ntp->type_ref) = dt;
	if (IsDataType(dt))
		for (ncp = note_cons; ncp->cons_name != nullptr; ncp++)
			for (cp = dt->dt_cons; cp != nullptr; cp = cp->c_next)
				if (cp->c_name == newstring(ncp->cons_name)) {
					if (ncp->cons_ref != nullptr)
						*(ncp->cons_ref) = cp;
					if (ncp->expr_ref != nullptr)
						*(ncp->expr_ref) =
							cons_expr(cp);
				}
}

/*
 *	Called at the end of the Standard module, to check that all the
 *	types and constructors required internally have been defined.
 */
void
check_type_defs(void)
{
	for (auto ntp = note_type; ntp->type_name != nullptr; ntp++)
		if (*(ntp->type_ref) == nullptr)
			error(LIBERR, "%s: standard type not defined",
				ntp->type_name);
	for (auto ncp = note_cons; ncp->cons_name != nullptr; ncp++) {
		if ((ncp->cons_ref != nullptr && *(ncp->cons_ref) == nullptr) ||
		    (ncp->expr_ref != nullptr && *(ncp->expr_ref) == nullptr))
			error(LIBERR, "%s: standard constructor not defined",
				ncp->cons_name);
	}
	if ((f_id = fn_local(newstring("id"))) == nullptr)
		error(LIBERR, "%s: standard function not defined", "id");
}
