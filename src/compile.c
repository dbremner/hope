#include "defs.h"
#include "compile.h"
#include "expr.h"
#include "cons.h"
#include "cases.h"
#include "char_array.h"
#include "path.h"
#include "error.h"

#define	MAX_MATCHES	60	/* max. constrs in a pattern (not checked) */
#define	MAX_PATHS	400	/* room for path storage (not checked) */

typedef	struct {
	short	level;
	Path	where;
	unsigned short	index, ncases;
} Match;

/* number and character cases are indicated by special values of ncases */

#define	NUMCASE	 10000	/* special ncases value: number match */
#define	CHARCASE 10001	/* special ncases value: character match */

#define	IsNumCase(m)	((m)->ncases == NUMCASE)
#define	IsCharCase(m)	((m)->ncases == CHARCASE)

static Match	*m_end;
static const	Match	*cur_match;
static int	cur_size;
static UCase	*new_body;	/* the new body */

static void	add_match(int level, Path where, Natural ncases, Natural c_index);
static void	gen_char_match(int level, Path here, Char c);
static void	gen_num_match(int level, Path here, Num n);
static Natural	num_cases(Cons *constr);
static void	gen_matches(int level, Path here, Expr *pattern);
static void	gen_match_constr(int level, Path *here_ptr, int arity, Expr *pattern);
static void	scan_formals(int level, Expr *formals);

static int	size_formals(Expr *formals);
static int	size_pattern(Expr *pattern);
static void	limb_map(LCase *e, EltMap *f);

static UCase	*gen_tree(const Match *matches, UCase *failure);
static UCase	*new_node(const Match *matches, UCase *failure, UCase *subtree);
static UCase	*merge(UCase *old);
static UCase	*sub_merge(UCase *old);
static UCase	*compile(UCase *old_body, Expr *pattern, Expr *new_expr);

/*
 * add another match to the list.
 */
static void
add_match(int level, Path where, Natural ncases, Natural c_index)
{
	m_end->level = level;
	m_end->where = p_save(p_reverse(where));
	m_end->ncases = ncases;
	m_end->index = c_index;
	m_end++;
}

static void
gen_char_match(int level, Path here, Char c)
{
	add_match(level, here, CHARCASE, (Natural)c);
}

static void
gen_num_match(int level, Path here, Num n)
{
	if (n > Zero) {
		add_match(level, here, NUMCASE, GREATER);
		gen_num_match(level, p_push(P_PRED, here), n-1);
	} else
		add_match(level, here, NUMCASE, EQUAL);
}

static Natural
num_cases(Cons *constr)
{
	while (constr->c_next != nullptr)
		constr = constr->c_next;
	return constr->c_index + 1;
}

/*
 * Generate the nodes of the matching tree given a path and a pattern.
 */
static void
gen_matches(int level, Path here, Expr *pattern)
{
	switch (pattern->e_class) {
	case expr_type::E_CHAR:
		gen_char_match(level, here, pattern->e_char);
        break;
    case expr_type::E_NUM:
		gen_num_match(level, here, pattern->e_num);
        break;
    case expr_type::E_CONS:
		ASSERT( pattern->e_const->c_nargs == 0 );
		add_match(level, here, num_cases(pattern->e_const),
			pattern->e_const->c_index);
        break;
    case expr_type::E_APPLY:
		gen_match_constr(level, &here, 0, pattern);
        break;
    case expr_type::E_PLUS:
		for (decltype(pattern->e_incr) i = 0; i < pattern->e_incr; i++) {
			add_match(level, here, NUMCASE, GREATER);
			here = p_push(P_PRED, here);
		}
		gen_matches(level, here, pattern->e_arg);
        break;
    case expr_type::E_PAIR:
		gen_matches(level, p_push(P_LEFT, here), pattern->e_left);
		gen_matches(level, p_push(P_RIGHT, here), pattern->e_right);
        break;
    case expr_type::E_VAR:
		;
        break;
    default:
		NOT_REACHED;
	}
}

/*
 * Similar wierd recursion to nv_constructor() (qv)
 */
static void
gen_match_constr(int level, Path *here_ptr, int arity, Expr *pattern)
{
	if (pattern->e_class == expr_type::E_CONS) {
		if (pattern->e_const == succ) {
			add_match(level, *here_ptr, NUMCASE, GREATER);
			*here_ptr = p_push(P_PRED, *here_ptr);
		} else {
			add_match(level, *here_ptr,
				num_cases(pattern->e_const),
				pattern->e_const->c_index);
			*here_ptr = p_push(P_STRIP, *here_ptr);
		}
	} else {
		ASSERT( pattern->e_class == expr_type::E_APPLY );
		gen_match_constr(level, here_ptr, arity+1, pattern->e_func);
		if (arity > 0) {
			gen_matches(level, p_push(P_LEFT, *here_ptr),
				pattern->e_arg);
			*here_ptr = p_push(P_RIGHT, *here_ptr);
		} else	/* last argument */
			gen_matches(level, *here_ptr, pattern->e_arg);
	}
}

static void
scan_formals(int level, Expr *formals)
{
	if (formals != nullptr && formals->e_class == expr_type::E_APPLY) {
		scan_formals(level+1, formals->e_func);
		gen_matches(level, p_new(), formals->e_arg);
	}
}

static int
size_formals(Expr *formals)
{
	int	n;

	n = 0;
	while (formals != nullptr && formals->e_class == expr_type::E_APPLY) {
		n += size_pattern(formals->e_arg);
		formals = formals->e_func;
	}
	return n;
}

static int
size_pattern(Expr *pattern)
{
	switch (pattern->e_class) {
	case expr_type::E_APPLY:
		return size_pattern(pattern->e_arg) + 1;
	case expr_type::E_PAIR:
		return size_pattern(pattern->e_left) +
			size_pattern(pattern->e_right);
	case expr_type::E_PLUS:
		return size_pattern(pattern->e_rest) + pattern->e_incr;
	case expr_type::E_NUM:
		return (int)(pattern->e_num) + 1;
	case expr_type::E_CONS:
    case expr_type::E_CHAR:
		return 1;
	case expr_type::E_VAR:
		return 0;
	default:
		NOT_REACHED;
	}
}

static void
limb_map(LCase *lcase, EltMap *f)
{
	if (lcase->lc_class == lc_type::LC_CHARACTER)
		ca_map(lcase->lc_c_limbs, f);
	else {
		auto finish = lcase->lc_limbs + lcase->lc_arity;
		for (auto this_ = lcase->lc_limbs; this_ != finish; this_++)
			*this_ = (*f)(*this_);
	}
}

/*
 * Generate the skinny matching tree from the given nodes,
 * patching in "new_body" at the leaf, and "failure" at each side branch.
 */
static UCase *
gen_tree(const Match *matches, UCase *failure)
{
	return matches == m_end ? new_body :
		new_node(matches, failure, gen_tree(matches+1, failure));
}

static UCase *
new_node(const Match *matches, UCase *failure, UCase *subtree)
{
	LCase	*limbs;

	if (IsCharCase(matches)) {
		limbs = char_case(failure);
		ca_assign(limbs->lc_c_limbs, matches->index, subtree);
	} else {
		limbs = IsNumCase(matches) ? num_case(failure) :
				alg_case(matches->ncases, failure);
		limbs->lc_limbs[matches->index] = subtree;
	}
	return ucase(matches->level, p_stash(matches->where), limbs);
}

/*
 * Given the current matching tree, merge it with the tree generated from
 * the given nodes and expression.
 */
static UCase *
merge(UCase *old)
{
	LCase	*lcase;

	switch (old->uc_class) {
    case uc_type::UC_F_NOMATCH:
    case uc_type::UC_L_NOMATCH:	/* do all the matching */
		return gen_tree(cur_match, old);
	case uc_type::UC_SUCCESS:
		if (old->uc_size < cur_size)	/* maybe more specific */
			return gen_tree(cur_match, old);
        break;
    case uc_type::UC_CASE:
		if (cur_match < m_end &&
		    (cur_match->level < old->uc_level ||
		     (cur_match->level == old->uc_level &&
		      p_less(cur_match->where, old->uc_path)))) {
			old->uc_references += old->uc_cases->lc_arity - 1;
			return new_node(cur_match, old, sub_merge(old));
		}
		if (old->uc_references > 1) {
			old->uc_references--;
			old = copy_ucase(old);
		}
		lcase = old->uc_cases;
		if (cur_match == m_end ||
		    old->uc_level < cur_match->level ||
		    (old->uc_level == cur_match->level &&
		     p_less(old->uc_path, cur_match->where)))
			limb_map(lcase, merge);
		else {	/* same place -- keep following */
			auto i = cur_match->index;
			if (lcase->lc_class == lc_type::LC_CHARACTER)
				ca_assign(lcase->lc_c_limbs, i,
					sub_merge(ca_index(
						lcase->lc_c_limbs, i)));
			else
				lcase->lc_limbs[i] =
					sub_merge(lcase->lc_limbs[i]);
		}
        break;
    default:
		NOT_REACHED;
	}
	return old;
}

static UCase *
sub_merge(UCase *old)
{
	cur_match++;
	old = merge(old);
	cur_match--;
	return old;
}

/*
 * Given the current body, generate the new body as dictated by the given
 * pattern and expression.
 */
static UCase *
compile(UCase *old_body, Expr *formals, Expr *new_expr)
{
	Match	matchlist[MAX_MATCHES];
	char	path_buf[MAX_PATHS];

	m_end = matchlist;
	p_init(path_buf, MAX_PATHS);
	scan_formals(0, formals);
	cur_match = matchlist;
	cur_size = size_formals(formals);
	new_body = success(new_expr, cur_size);
	return old_body == nullptr ? new_body : merge(old_body);
}

UCase *
comp_branch(UCase *old_body, Branch *branch)
{
	comp_expr(branch->br_expr);
	return compile(old_body, branch->br_formals, branch->br_expr);
}

/*
 *	Compile all the LAMBDAs in expr.
 */
void
comp_expr(Expr *expr)
{
	switch (expr->e_class) {
	case expr_type::E_LAMBDA:
    case expr_type::E_EQN:
    case expr_type::E_PRESECT:
    case expr_type::E_POSTSECT:
		expr->e_code = l_nomatch(expr);
		for (auto br = expr->e_branch; br != nullptr; br = br->br_next)
			expr->e_code = comp_branch(expr->e_code, br);
        break;
    case expr_type::E_PAIR:
		comp_expr(expr->e_left);
		comp_expr(expr->e_right);
        break;
    case expr_type::E_APPLY:
    case expr_type::E_IF:
    case expr_type::E_WHERE:
    case expr_type::E_LET:
    case expr_type::E_RWHERE:
    case expr_type::E_RLET:
		comp_expr(expr->e_func);
		comp_expr(expr->e_arg);
        break;
    case expr_type::E_MU:
		comp_expr(expr->e_body);
        break;
    case expr_type::E_NUM:
    case expr_type::E_CHAR:
    case expr_type::E_CONS:
    case expr_type::E_DEFUN:
    case expr_type::E_PARAM:
		;
        break;
    default:
		NOT_REACHED;
	}
}
