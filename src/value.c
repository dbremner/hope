#include "defs.h"
#include "value.h"

Cell *
new_pair(Cell *left, Cell *right)
{
	auto cp = new_cell(C_PAIR);
	cp->c_left = left;
	cp->c_right = right;
	return cp;
}

Cell *
new_dirs(Path path, Cell *val)
{
	auto cp = new_cell(C_DIRS);
	cp->c_path = path;
	cp->c_val = val;
	return cp;
}

Cell *
new_cons(Cons *data_constructor, Cell *arg)
{
	auto cp = new_cell(C_CONS);
	cp->c_cons = data_constructor;
	cp->c_arg = arg;
	return cp;
}

Cell *
new_susp(Expr *expr, Cell *env)
{
	auto cp = new_cell(C_SUSP);
	cp->c_expr = expr;
	cp->c_env = env;
	return cp;
}

Cell *
new_papp(Expr *expr, Cell *env, int arity)
{
	auto cp = new_cell(C_PAPP);
	cp->c_expr = expr;
	cp->c_env = env;
	cp->c_arity = arity;
	return cp;
}

Cell *
new_ucase(UCase *code, Cell *env)
{
	auto cp = new_cell(C_UCASE);
	cp->c_code = code;
	cp->c_env = env;
	return cp;
}

Cell *
new_lcase(LCase *lcase, Cell *env)
{
	auto cp = new_cell(C_LCASE);
	cp->c_lcase = lcase;
	cp->c_env = env;
	return cp;
}

Cell *
new_cnst(Cons *data_constant)
{
	auto cp = new_cell(C_CONST);
	cp->c_cons = data_constant;
	return cp;
}

Cell *
new_num(Num n)
{
	auto cp = new_cell(C_NUM);
	cp->c_num = n;
	return cp;
}

Cell *
new_char(Char c)
{
	auto cp = new_cell(C_CHAR);
	cp->c_char = c;
	return cp;
}

Cell *
new_stream(FILE *f)
{
	auto cp = new_cell(C_STREAM);
	cp->c_file = f;
	return cp;
}
