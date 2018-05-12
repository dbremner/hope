#include "defs.h"
#include "table.h"
#include "newstring.h"

void
t_init(Table *table)
{
	table->t_front = nullptr;
	table->t_end = &(table->t_front);
}

void
t_insert(Table *table, TabElt *element)
{
	*(table->t_end) = element;
	table->t_end = &(element->t_next);
	element->t_next = nullptr;
}

void
t_delete(Table *table, TabElt *element)
{
	TabElt	**ep;

	for (ep = &(table->t_front); *ep != nullptr; ep = &((*ep)->t_next))
		if (*ep == element) {
			*ep = (*ep)->t_next;
			if (*ep == nullptr)
				table->t_end = ep;
			return;
		}
}

void
t_copy(Table *table1, const Table *table2)
{
	if (table2->t_front == nullptr)
		t_init(table1);
	else {
		table1->t_front = table2->t_front;
		table1->t_end = table2->t_end;
	}
}

TabElt *
t_lookup(const Table *table, String name)
{
	TabElt	*elem;

	for (elem = table->t_front; elem != nullptr; elem = elem->t_next)
		if (elem->t_name == name)
			return elem;
	return nullptr;
}

void
t_foreach(const Table *table, TableAction *action)
{
	TabElt	*elem;

	for (elem = table->t_front; elem != nullptr; elem = elem->t_next)
		(*action)(elem);
}
