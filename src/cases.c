#include "defs.h"
#include "cases.h"
#include "expr.h"
#include "char_array.h"
#include "memory.h"

static LCase	*copy_lcase(LCase *old);
static UCase	*new_reference(UCase *node);

/*
 *	Upper case expressions.
 */

UCase *
ucase(int level, Path path, LCase *cases)
{
	auto code = NEW(UCase);
	code->uc_class = uc_type::UC_CASE;
	code->uc_references = 1;
	code->uc_level = level;
	code->uc_path = path;
	code->uc_cases = cases;
	return code;
}

UCase *
f_nomatch(Func *defun)
{
	auto code = NEW(UCase);
	code->uc_class = uc_type::UC_F_NOMATCH;
	code->uc_defun = defun;
	return code;
}

UCase *
l_nomatch(Expr *who)
{
	auto code = NEW(UCase);
	code->uc_class = uc_type::UC_L_NOMATCH;
	code->uc_who = who;
	return code;
}

UCase *
success(Expr *body, int size)
{
	auto code = NEW(UCase);
	code->uc_class = uc_type::UC_SUCCESS;
	code->uc_body = body;
	code->uc_size = size;
	return code;
}

UCase *
strict(Expr *real)
{
	auto code = NEW(UCase);
	code->uc_class = uc_type::UC_STRICT;
	code->uc_real = real;
	return code;
}

UCase *
copy_ucase(UCase *old)
{
	auto new_ucase = NEW(UCase);
	new_ucase->uc_class = old->uc_class;
	switch (old->uc_class) {
	case uc_type::UC_CASE:
		new_ucase->uc_references = 1;
		new_ucase->uc_level = old->uc_level;
		new_ucase->uc_path = old->uc_path;
		new_ucase->uc_cases = copy_lcase(old->uc_cases);
        break;
    case uc_type::UC_F_NOMATCH:
		new_ucase->uc_defun = old->uc_defun;
        break;
    case uc_type::UC_L_NOMATCH:
		new_ucase->uc_who = old->uc_who;
        break;
    case uc_type::UC_SUCCESS:
		new_ucase->uc_body = old->uc_body;
		new_ucase->uc_size = old->uc_size;
        break;
    case uc_type::UC_STRICT:
		new_ucase->uc_real = old->uc_real;
        break;
	}
	return new_ucase;
}

/*
 *	Lower case expressions.
 */

LCase *
alg_case(Natural arity, UCase *def)
{
	auto lcase = NEW(LCase);
	lcase->lc_class = lc_type::LC_ALGEBRAIC;
	lcase->lc_arity = arity;
	lcase->lc_limbs = NEWARRAY(UCase *, arity);
	for (decltype(arity) i = 0; i < arity; i++)
		lcase->lc_limbs[i] = def;
	return lcase;
}

LCase *
num_case(UCase *def)
{
	auto lcase = alg_case((Natural)3, def);
	lcase->lc_class = lc_type::LC_NUMERIC;
	return lcase;
}

LCase *
char_case(UCase *def)
{
	auto lcase = NEW(LCase);
	lcase->lc_class = lc_type::LC_CHARACTER;
	lcase->lc_arity = 256;		/* number of characters */
	lcase->lc_c_limbs = ca_new(def);
	return lcase;
}

static LCase *
copy_lcase(LCase *old)
{
	auto new_lcase = NEW(LCase);
	new_lcase->lc_class = old->lc_class;
	new_lcase->lc_arity = old->lc_arity;
	switch (old->lc_class) {
	case lc_type::LC_ALGEBRAIC:
    case lc_type::LC_NUMERIC:
		new_lcase->lc_limbs = NEWARRAY(UCase *, old->lc_arity);
		for (decltype(old->lc_arity) i = 0; i < old->lc_arity; i++)
			new_lcase->lc_limbs[i] = new_reference(old->lc_limbs[i]);
        break;
    case lc_type::LC_CHARACTER:
		new_lcase->lc_c_limbs = ca_copy(old->lc_c_limbs);
		ca_map(new_lcase->lc_c_limbs, new_reference);
        break;
	}
	return new_lcase;
}

static UCase *
new_reference(UCase *node)
{
	if (node->uc_class == uc_type::UC_CASE)
		node->uc_references++;
	return node;
}
