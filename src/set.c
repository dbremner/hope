#include "defs.h"
#include "set.h"

void
set_clear(SetPtr set, Natural n)
{
	while (n-- > 0)
		*set++ = 0;
}

Natural
set_card(SetPtr set, Natural n)
{
	Natural	count;
	Natural	bits;

	count = 0;
	while (n-- > 0)
		for (bits = *set++; bits != 0; bits >>= 1)
			if ((bits & 01) != 0)
				count++;
	return count;
}

void
set_union(SetPtr set1, Natural n1, SetPtr set2, Natural n2)
{
	if (n2 < n1)
		n1 = n2;
	while (n1-- > 0)
		*set1++ |= *set2++;
}
