#ifndef	NUM_H
#define	NUM_H

#include "defs.h"

/*
 *	The representation of the type `num'.
 */

#include <float.h>
#include <math.h>
#define	Num	double
#define	atoNUM	atof
#define	NUMfmt	"%.*g", DBL_DIG
#define	Zero	0.0

extern	Num	atoNUM(const char *s);

#endif
