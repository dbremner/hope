#ifndef PR_TY_VALUE_H
#define PR_TY_VALUE_H

/*
 *	Printing of inferred types.
 */

#include "defs.h"

void init_pr_ty_value(void);

void pr_ty_value(FILE *f, Cell *type);

#endif
