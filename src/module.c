#include "defs.h"
#include "module.h"
#include "expr.h"
#include "deftype.h"
#include "cons.h"
#include "typevar.h"
#include "op.h"
#include "newstring.h"
#include "set.h"
#include "table.h"
#include "error.h"
#include "memory.h"
#include "names.h"
#include "source.h"
#include "builtin.h"
#include "compare.h"
#include "remember_type.h"
#include "output.h"
#include "pr_expr.h"
#include "pr_type.h"

#define	MAX_MODULES	32	/* max. no. of modules (checked) */
#define	MAX_TVARS	32	/* max. no. of type variables (checked) */
#define	MAX_MODNAME	100	/* max. len. of module path (checked) */

#ifdef	unix
#	define	DIR_SEPARATOR	'/'
#	define	PATH_SEPARATOR	':'
#else
#ifdef	msdos
#	define	DIR_SEPARATOR	'\\'
#	define	PATH_SEPARATOR	';'
#else
#endif
#endif

#ifdef	DIR_SEPARATOR
#	include "hopelib.h"
#endif

#define	SESSION_NAME	"<Session>"
#define	STANDARD_NAME	"Standard"

static const	char	extension[] = ".hop";

static const	char	**dir;

typedef	struct _Module	Module;
struct _Module {
	String	mod_name;
	unsigned short	mod_num;	/* index in module table */
	SET(mod_uses, MAX_MODULES);
	SET(mod_all_uses, MAX_MODULES);	/* transitive closure of uses */
	SET(mod_tvars, MAX_TVARS);
	SET(mod_all_tvars, MAX_TVARS);	/* includes tvars in used modules */
	Table	mod_ops;
	Table	mod_types;
	Table	mod_fns;
	Module	*mod_public;		/* public part of a private module */
};

TVar	alpha;

static String	Standard_name;

/* special modules */
#define SESSION 0	/* the terminal interaction */
#define	STANDARD 1	/* standard environment */
#define	ORDINARY 2	/* first ordinary module */
/*
 *	List of all modules ever encountered.
 */
static Module	*mod_list[MAX_MODULES];
static Natural	mod_count;
static SET(mod_unread, MAX_MODULES);	/* modules yet to be read */
/*
 *	Stack of modules being read to satisfy `uses' commands.
 *	The top module is the one currently being read; the bottom one is
 *	the current session.
 */
static Module	*mod_stack[MAX_MODULES];
static Module	**mod_current;

static FILE	*disp_file;

static TVar	tvar_list[MAX_TVARS];
static Natural	tvar_count;

typedef	void	*LookFn(String name, Module *mod);

static Module	*mod_new(String name);
static void	mod_clear(Module *mod);
static void	mod_copy(Module *mod1, Module *mod2);
static Module	*module(String name);
static void	mark_as_private(TabElt *p);
static void	reset_private(TabElt *p);
static void	fix_syn_depth(TabElt *p);
static void	us_display(Module *mod);
static Bool	mod_read(Module *mod);
static FILE	*mod_create(String name);

static void	*look_here(String name, LookFn *look_fn);
static void	*look_everywhere(String name, LookFn *look_fn);

static void	*op_mod_lookup(String name, Module *mod);
static void	op_display(TabElt *p);

static void	tv_display(Module *mod);
static String	tv_trim(String name);
static TVar	tv_var(Natural n);

static void	*dt_mod_lookup(String name, Module *mod);
static void	*cons_mod_lookup(String name, Module *mod);
static void	ty_cons_lookup(TabElt *p);
static void	ty_display(TabElt *p);
static void	ty_abs_display(TabElt *p);
static void	ty_def_display(TabElt *p);
static void	pr_fdecl(FILE *f, Func *fn);
static void	*fn_mod_lookup(String name, Module *mod);
static void	fn_display(TabElt *p);
static void	dec_display(TabElt *p);
static void	def_display(TabElt *p);
static void	dec_functor(DefType *dt);

static void	split_path(void);
static void	default_path(void);

/*
 *	Set up the special modules
 */
void
mod_init(void)
{
	split_path();
	tvar_count = 0;
	mod_count = 0;
	CLEAR(mod_unread);
	mod_current = mod_stack;
	*mod_current = mod_new(newstring(SESSION_NAME));
	Standard_name = newstring(STANDARD_NAME);
	mod_use(Standard_name);
	mod_fetch();
}

static void
split_path(void)
{
	int	n;
#ifdef PATH_SEPARATOR
	const	char	*path;

	path = getenv("HOPEPATH");
	if (path == nullptr)
		default_path();
	else {
		char	*np;
		const	char	*pp;

		n = 0;
		for (pp = path; *pp != '\0'; pp++)
			if (*pp == PATH_SEPARATOR)
				n++;
		dir = NEWARRAY(const char *, n+2);
		np = NEWARRAY(char, strlen(path)+1);
		dir[0] = np;
		n = 1;
		for (pp = path; *pp != '\0'; pp++)
			if (*pp == PATH_SEPARATOR) {
				*np++ = '\0';
				dir[n] = np;
				n++;
			} else
				*np++ = *pp;
		*np = '\0';
		dir[n] = nullptr;
	}
#else
	default_path();
#endif
#ifdef HOPELIB
	/* empty entries in the path stand for HOPELIB */
	for (n = 0; dir[n] != nullptr; n++)
		if (dir[n][0] == '\0')
			dir[n] = HOPELIB;
#else
	/* empty entries in the path stand for . */
	for (n = 0; dir[n] != nullptr; n++)
		if (dir[n][0] == '\0')
			dir[n] = ".";
#endif
}

static void
default_path(void)
{
	dir = NEWARRAY(const char *, 3);
	dir[0] = ".";
	dir[1] = "";
	dir[2] = nullptr;
}

String
mod_name(void)
{
	return (*mod_current)->mod_name;
}

Bool
mod_system(void)
{
	return (*mod_current)->mod_num == STANDARD;
}

static Module *
mod_new(String name)
{
	if (mod_count == MAX_MODULES) {
		error(SEMERR, "too many modules");
		return nullptr;
	}
	auto mod = NEW(Module);
	mod->mod_name = name;
	mod->mod_num = mod_count;
	mod_clear(mod);
	mod_list[mod_count++] = mod;
	return mod;
}

static void
mod_clear(Module *mod)
{
	CLEAR(mod->mod_uses);
	CLEAR(mod->mod_all_uses);
	CLEAR(mod->mod_tvars);
	CLEAR(mod->mod_all_tvars);
	t_init(&(mod->mod_ops));
	t_init(&(mod->mod_types));
	t_init(&(mod->mod_fns));
	mod->mod_public = nullptr;
}

static void
mod_copy(Module *mod1, Module *mod2)
{
	UNION(mod1->mod_uses, mod2->mod_uses);
	UNION(mod1->mod_all_uses, mod2->mod_all_uses);
	UNION(mod1->mod_tvars, mod2->mod_tvars);
	UNION(mod1->mod_all_tvars, mod2->mod_all_tvars);
	t_copy(&(mod1->mod_ops), &(mod2->mod_ops));
	t_copy(&(mod1->mod_types), &(mod2->mod_types));
	t_copy(&(mod1->mod_fns), &(mod2->mod_fns));
	mod1->mod_public = mod2->mod_public;
}

static Module *
module(String name)
{
	for (decltype(mod_count) i = STANDARD; i < mod_count; i++)
		if (mod_list[i]->mod_name == name)
			return mod_list[i];	/* already here */
	/* not here -- make a note to read it */
	auto mod = mod_new(name);
	if (mod != nullptr)
		ADD(mod_unread, mod->mod_num);
	return mod;
}

void
mod_use(String name)
{
	auto mod = module(name);
	if (mod == nullptr)
		return;
	for (auto mp = mod_stack; mp <= mod_current; mp++)
		if (*mp == mod || (*mp)->mod_public == mod) {
			error(SEMERR, "'%s': cyclic 'uses' reference",
				mod->mod_name);
			return;
		}
	ADD((*mod_current)->mod_uses, mod->mod_num);
	ADD((*mod_current)->mod_all_uses, mod->mod_num);
	UNION((*mod_current)->mod_all_uses, mod->mod_all_uses);
	UNION((*mod_current)->mod_all_tvars, mod->mod_all_tvars);
}

/*
 *	Read in any queued modules
 */
void
mod_fetch(void)
{
	/* unprocessed uses */
	for (decltype(mod_count) i = STANDARD; i < mod_count; i++)
		if (MEMBER(mod_unread, i) &&
		    MEMBER((*mod_current)->mod_uses, i)) {
			if (mod_read(mod_list[i])) {
				REMOVE(mod_unread, i);
				/* go read this one */
				*++mod_current = mod_list[i];
				if (i != STANDARD)
					mod_use(Standard_name);
				return;
			} else {
				error(i == STANDARD ? LIBERR : SEMERR,
					"'%s': can't read module",
					mod_list[i]->mod_name);
				REMOVE((*mod_current)->mod_uses, i);
			}
		}
}

static void
mark_as_private(TabElt *p)
{
	auto dt = (DefType *)p;
	dt->dt_private = IsAbsType(dt);
	if (dt->dt_private)
		dt->dt_oldvarlist = dt->dt_varlist;
}

/*
 *	Rest of module is to be hidden from other modules.
 */
void
mod_private(void)
{
	Module	*priv_module;

	if ((*mod_current)->mod_num == SESSION)	/* at top level -- no effect */
		return;
	/*
	 * Start a new module (which will be discarded) for any new
	 * definitions.
	 */
	priv_module = mod_new((*mod_current)->mod_name);
	if (priv_module == nullptr)
		return;
	UNION(priv_module->mod_uses, (*mod_current)->mod_uses);
	UNION(priv_module->mod_all_uses, (*mod_current)->mod_all_uses);
	UNION(priv_module->mod_tvars, (*mod_current)->mod_tvars);
	UNION(priv_module->mod_all_tvars, (*mod_current)->mod_all_tvars);
	priv_module->mod_public = *mod_current;
	/*
	 * Mark all abstype's in the current module so they can be reset
	 * at the end of the module.
	 */
	t_foreach(&((*mod_current)->mod_types), mark_as_private);

	*mod_current = priv_module;
	preserve();
}

/*
 *	Reset data types and type synonyms defined after the "private".
 */
static void
reset_private(TabElt *p)
{
	auto dt = (DefType *)p;
	if (dt->dt_private) {
		dt->dt_varlist = dt->dt_oldvarlist;
		dt->dt_syn_depth = 0;
		dt->dt_cons = nullptr;
	}
}

/*
 *	Recompute dt_syn_depth for all type synonyms in the current module.
 *	This must be done:
 *	- when an abstype is defined as a synonym, and
 *	- when privately-defined abstype's are reset.
 */
void
fix_synonyms(void)
{
	auto current = *mod_current;
	t_foreach(&(current->mod_types), fix_syn_depth);
	if (current->mod_public != nullptr)
		t_foreach(&(current->mod_public->mod_types), fix_syn_depth);
}

static void
fix_syn_depth(TabElt *p)
{
	int	n;
	Type	*type;

	auto dt = (DefType *)p;
	n = 0;
	for (auto syn = dt; IsSynType(syn); syn = type->ty_deftype) {
		n++;
		type = syn->dt_type;
		while (type->ty_class == TY_MU)
			type = type->ty_body;
		if (type->ty_class == TY_VAR)
			break;
	}
	dt->dt_syn_depth = n;
}

/* end of use */
void
mod_finish(void)
{
	auto current = *mod_current;
	if (current->mod_num == STANDARD ||
	    (current->mod_public != nullptr &&
	     current->mod_public->mod_num == STANDARD)) {
		check_type_defs();
		alpha = tv_var((Natural)0);
		init_cmps();
		init_builtins();
		init_print();
		init_argv();
		preserve();
	}
	if (current->mod_public != nullptr) {
		current = current->mod_public;
		/*
		 * Reset any abstype's that have been privately defined
		 */
		t_foreach(&(current->mod_types), reset_private);
		fix_synonyms();
	}
	for (auto mp = mod_stack; mp < mod_current; mp++)
		if (MEMBER((*mp)->mod_uses, current->mod_num)) {
			UNION((*mp)->mod_all_uses, current->mod_all_uses);
			UNION((*mp)->mod_all_tvars, current->mod_all_tvars);
		}
	mod_current--;
	mod_fetch();
}

void
mod_save(String name)
{
	if (restricted) {
		error(SEMERR, "'save' command disabled");
		return;
	}
	if ((*mod_current)->mod_num != SESSION) {
		error(SEMERR, "'save' not permitted in module");
		return;
	}
	auto f = mod_create(name);
	if (f == nullptr)
		return;
	mod_dump(f);

	/* move the contents of the current session into the module */
	auto mod = mod_new(name);
	if (mod == nullptr)
		return;
	mod_copy(mod, *mod_current);
	mod_clear(*mod_current);
	mod_use(Standard_name);
	mod_use(name);

	preserve();
}

void
mod_dump(FILE *f)
{
	if (f != nullptr) {
		disp_file = f;
		us_display(mod_list[SESSION]);
		tv_display(mod_list[SESSION]);
		t_foreach(&(mod_list[SESSION]->mod_ops), op_display);
		t_foreach(&(mod_list[SESSION]->mod_types), ty_abs_display);
		t_foreach(&(mod_list[SESSION]->mod_types), ty_def_display);
		t_foreach(&(mod_list[SESSION]->mod_fns), dec_display);
		t_foreach(&(mod_list[SESSION]->mod_fns), def_display);
		(void)fclose(disp_file);
	}
}

void
mod_file(char *buf, size_t len, String name)
{
	(void)snprintf(buf, len, "%s%s", name, extension);
}

static void
us_display(Module *mod)
{
	Bool	first;

	if (CARD(mod->mod_uses) > 1) {	/* any uses apart from STANDARD? */
		(void)fprintf(disp_file, "%s ", n_uses);
		first = TRUE;
		for (decltype(mod_count) i = ORDINARY; i < mod_count; i++)
			if (MEMBER(mod->mod_uses, i)) {
				(void)fprintf(disp_file, first ? "%s" : ", %s",
					mod_list[i]->mod_name);
				first = FALSE;
			}
		(void)fprintf(disp_file, ";\n");
	}
}

/*
 *	Open a module for reading.
 *	The module is sought in each directory of the path in turn.
 */
static Bool
mod_read(Module *mod)
{
	FILE	*f;
	char	filename[MAX_MODNAME];
#ifdef DIR_SEPARATOR
	const	char	*const	*dp;
#endif

	f = nullptr;
#ifdef DIR_SEPARATOR
	for (dp = dir; *dp != nullptr; dp++)
		if (strlen(*dp) + 1 + strlen(filename) < MAX_MODNAME) {
		(void)snprintf(filename, sizeof(filename), "%s%c%s%s", *dp, DIR_SEPARATOR, mod->mod_name, extension);
		f = fopen(filename, "r");
		if (f != nullptr)
			break;
	}
#else
	mod_file(filename, sizeof(filename), mod->mod_name);
	f = fopen(filename, "r");
#endif
	if (f == nullptr)
		return FALSE;
	enterfile(f);
	return TRUE;
}

/*
 *	Create a new module in the current directory.
 */
static FILE *
mod_create(String name)
{
	FILE	*f;
	char	filename[MAX_MODNAME];

	mod_file(filename, sizeof(filename), name);
	if ((f = fopen(filename, "r")) != nullptr) {
		(void)fclose(f);
		error(SEMERR, "'%s': a module with this name already exists",
			name);
		return nullptr;
	}
	if ((f = fopen(filename, "w")) == nullptr)
		error(SEMERR, "'%s': can't save module", name);
	return f;
}

/*
 *	Look for a name in the current module
 */
static void *
look_here(String name,
		void * (*look_fn)(String name, Module *mod))
{
	void	*found;

	if ((found = (*look_fn)(name, *mod_current)) != nullptr)
		return found;
	if ((*mod_current)->mod_public != nullptr)
		return (*look_fn)(name, (*mod_current)->mod_public);
	return nullptr;
}

/*
 *	Look for a name in the current module and all those it uses
 */
static void *
look_everywhere(String name,
		void * (*look_fn)(String name, Module *mod))
{
	void	*found;

	if ((found = look_here(name, look_fn)) != nullptr)
		return found;
	for (auto i = mod_count-1; i >= STANDARD; i--)
		if (MEMBER((*mod_current)->mod_all_uses, i) &&
		    (found = (*look_fn)(name, mod_list[i])) != nullptr)
			return found;
	return nullptr;
}

/*
 *	Operators
 */

void
op_declare(String name, int prec, Assoc assoc)
{
	Op	*op;

	op = NEW(Op);
	op->op_name = name;
	op->op_prec = prec < MINPREC ? MINPREC :
		      prec > MAXPREC ? MAXPREC : prec;
	op->op_assoc = assoc;
	t_insert(&((*mod_current)->mod_ops), (TabElt *)op);
}

static void *
op_mod_lookup(String name, Module *mod)
{
	return (void *)t_lookup(&(mod->mod_ops), name);
}

Op *
op_lookup(String name)
{
	return (Op *)look_everywhere(name, op_mod_lookup);
}

static void
op_display(TabElt *p)
{
	auto op = (Op *)p;
	(void)fprintf(disp_file, "%s %s : %d;\n",
		op->op_assoc == ASSOC_LEFT ? n_infix : n_infixr,
		op->op_name, op->op_prec);
}

/*
 *	Type Variables.
 *
 *	Since the system prints out types using primed type variables
 *	when it runs out of type variables, primes are ignored when
 *	declaring type variables or checking whether they are declared.
 */

void
tv_declare(String name)
{
	decltype(tvar_count) n;

	name = tv_trim(name);
	for (n = 0; n < tvar_count && tvar_list[n] != name; n++)
		;
	if (n == tvar_count) {		/* add it */
		if (tvar_count == MAX_TVARS) {
			error(SEMERR, "too many type variables");
			return;
		}
		tvar_list[tvar_count++] = name;
	}
	ADD((*mod_current)->mod_tvars, n);
	ADD((*mod_current)->mod_all_tvars, n);
}

Bool
tv_lookup(String name)
{
	name = tv_trim(name);
	for (decltype(tvar_count) n = 0; n < tvar_count; n++)
		if (tvar_list[n] == name &&
		    MEMBER((*mod_current)->mod_all_tvars, n))
			return TRUE;
	return FALSE;
}

/*
 *	Remove primes from a type variable identifier.
 */
static String
tv_trim(String name)
{
	const	char	*s;

	for (s = name; *s != '\0'; s++)
		if (*s == '\'')
			return newnstring(name, s-name);
	return name;
}

/*
 *	Print n as the n+1'th known type variable, adding primes when the
 *	known ones run out.
 */
void
tv_print(FILE *f, Natural n)
{
	int	ntvars;

	/* get the number of type variables visible in the current module */
	ntvars = CARD((*mod_current)->mod_all_tvars);
	(void)fprintf(f, "%s", tv_var(n%ntvars));
	for (n = n/ntvars; n > 0; n--)
		(void)fprintf(f, "'");
}

/*
 *	The n'th type variable visible in the current module.
 *	There must be at least 2 (alpha and beta);
 *	if n is too large, wrap around.
 */
static TVar
tv_var(Natural n)
{
	SetPtr	known;

	ASSERT( tvar_count > 0 );
	ASSERT( CARD((*mod_current)->mod_all_tvars) > 0 );
	known = (*mod_current)->mod_all_tvars;
	auto tvn = tvar_count-1;
	do {
		do {
			tvn = (tvn+1)%tvar_count;
		} while (! MEMBER(known, tvn));
	} while (n-- > 0);
	return tvar_list[tvn];
}

static void
tv_display(Module *mod)
{
	Bool	first;

	if (CARD(mod->mod_tvars) > 0) {
		(void)fprintf(disp_file, "%s ", n_typevar);
		first = TRUE;
		for (decltype(tvar_count) n = 0; n < tvar_count; n++)
			if (MEMBER(mod->mod_tvars, n)) {
				(void)fprintf(disp_file, first ? "%s" : ", %s",
					tvar_list[n]);
				first = FALSE;
			}
		(void)fprintf(disp_file, ";\n");
	}
}

/*
 *	Defined types and constructors.
 */

void
dt_declare(DefType *dt)
{
	t_insert(&((*mod_current)->mod_types), (TabElt *)dt);
	if ((*mod_current)->mod_num == STANDARD)
		remember_type(dt);
	dec_functor(dt);
}

static void *
dt_mod_lookup(String name, Module *mod)
{
	return (void *)t_lookup(&(mod->mod_types), name);
}

DefType *
dt_lookup(String name)
{
	return (DefType *)look_everywhere(name, dt_mod_lookup);
}

DefType *
dt_local(String name)
{
	return (DefType *)look_here(name, dt_mod_lookup);
}

static Cons	*cons_found;
static String	cons_name;

static void
ty_cons_lookup(TabElt *p)
{
	Cons	*cp;
	auto dt = (DefType *)p;
	if (IsDataType(dt))
		for (cp = dt->dt_cons; cp != nullptr; cp = cp->c_next)
			if (cp->c_name == cons_name) {
				cons_found = cp;
				return;
			}
}

static void *
cons_mod_lookup(String name, Module *mod)
{
	cons_name = name;
	cons_found = nullptr;
	t_foreach(&(mod->mod_types), ty_cons_lookup);
	return (void *)cons_found;
}

Cons *
cons_lookup(String name)
{
	return (Cons *)look_everywhere(name, cons_mod_lookup);
}

Cons *
cons_local(String name)
{
	return (Cons *)look_here(name, cons_mod_lookup);
}

static void
ty_display(TabElt *p)
{
	pr_deftype(disp_file, (DefType *)p, TRUE);
}

static void
ty_abs_display(TabElt *p)
{
	pr_deftype(disp_file, (DefType *)p, FALSE);
}

static void
ty_def_display(TabElt *p)
{
	DefType	*dt;

	dt = (DefType *)p;
	if (! IsAbsType(dt))
		pr_deftype(disp_file, dt, TRUE);
}

/*
 *	Functions.
 */

void
new_fn(String name, QType *qtype)
{
	Func	*fn;

	fn = NEW(Func);
	fn->f_name = name;
	fn->f_arity = 0;
	fn->f_explicit_dec = TRUE;
	fn->f_explicit_def = FALSE;
	fn->f_qtype = qtype;
	fn->f_branch = nullptr;
	fn->f_code = nullptr;
	t_insert(&((*mod_current)->mod_fns), (TabElt *)fn);
}

static void
dec_functor(DefType *dt)
{
	Func	*fn;

	fn = NEW(Func);
	fn->f_name = dt->dt_name;
	fn->f_arity = 0;
	fn->f_explicit_dec = FALSE;
	fn->f_explicit_def = FALSE;
	fn->f_tycons = dt;
	fn->f_branch = nullptr;
	fn->f_code = nullptr;
	t_insert(&((*mod_current)->mod_fns), (TabElt *)fn);
}

void
del_fn(Func *fn)
{
	t_delete(&((*mod_current)->mod_fns), (TabElt *)fn);
}

static void *
fn_mod_lookup(String name, Module *mod)
{
	return (void *)t_lookup(&(mod->mod_fns), name);
}

Func *
fn_lookup(String name)
{
	return (Func *)look_everywhere(name, fn_mod_lookup);
}

Func *
fn_local(String name)
{
	return (Func *)look_here(name, fn_mod_lookup);
}

static void
fn_display(TabElt *p)
{
	Func	*fn;

	fn = (Func *)p;
	if (fn->f_explicit_dec || fn->f_explicit_def) {
		(void)fprintf(disp_file, "\n");
		pr_fdecl(disp_file, fn);
		pr_fundef(disp_file, fn);
	}
}

static void
dec_display(TabElt *p)
{
	Func	*fn;

	fn = (Func *)p;
	if (fn->f_explicit_dec)
		pr_fdecl(disp_file, fn);
}

static void
def_display(TabElt *p)
{
	Func	*fn;

	fn = (Func *)p;
	if (fn->f_explicit_def) {
		(void)fprintf(disp_file, "\n");
		pr_fundef(disp_file, fn);
	}
}

static void
pr_fdecl(FILE *f, Func *fn)
{
	(void)fprintf(f, "%s %s : ", n_dec, fn->f_name);
	pr_type(f, fn->f_type);
	(void)fprintf(f, ";\n");
}

/*
 *	List out all tables.
 */
void
display(void)
{
	if ((*mod_current)->mod_num != SESSION) {
		error(SEMERR, "'display' not permitted in module");
		return;
	}
	disp_file = stdout;
	us_display(mod_list[SESSION]);
	tv_display(mod_list[SESSION]);
	t_foreach(&(mod_list[SESSION]->mod_ops), op_display);
	t_foreach(&(mod_list[SESSION]->mod_types), ty_display);
	t_foreach(&(mod_list[SESSION]->mod_fns), fn_display);
}
