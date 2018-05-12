/*
 *	Lexical analysis
 */
#include "defs.h"
#include "op.h"
#include "newstring.h"
#include "num.h"
#include "text.h"
#include "source.h"
#include "error.h"
#include "typevar.h"
#include "names.h"
#include "yyparse.h"

#define	MAX_STRING	100	/* max. size of string (checked) */

String	n_or, n_valof, n_is, n_eq, n_gives,
		n_abstype, n_data, n_dec,
		n_infix, n_infixr, n_type, n_typevar, n_uses,
		n_else, n_if, n_in, n_lambda, n_let,
		n_letrec, n_mu, n_then, n_where, n_whererec;
String	n_pos, n_neg, n_none;

typedef struct {
	const	char	*sym_name;	/* string to match -- NULL for end */
	short	sym_token;		/* token returned to yacc */
} Symbol;

/* reserved identifiers */
static Symbol	reserved[] = {
#ifdef UTF_LIBS
	{ "\xce\xbb",	LAMBDA	},
	{ "\xce\xbc",	MU	},
	{ "\xe2\x8a\x95", OR	},
	{ "\xe2\x87\x90", IS	},
	{ "\xe2\x87\x92", GIVES	},
	{ "\xe2\x89\x9c", DEFEQ	},
#endif
	{ "++",		OR	},
	{ "---",	VALOF	},
	{ ":",		':'	},
	{ "<=",		IS	},
	{ "==",		DEFEQ	},
	{ "=>",		GIVES	},
	{ "abstype",	ABSTYPE	},
	{ "data",	DATA	},
	{ "dec",	DEC	},
	{ "display",	DISPLAY	},
#ifdef	RE_EDIT
	{ "edit",	EDIT	},
#endif
	{ "else",	ELSE	},
	{ "end",	END	},	/* for sideways compatability */
	{ "exit",	EXIT	},
	{ "if",		IF	},
	{ "in",		IN	},
	{ "infix",	INFIX	},
	{ "infixr",	INFIXR	},
	{ "infixrl",	INFIXR	},	/* for backward compatability */
	{ "lambda",	LAMBDA	},
	{ "let",	LET	},
	{ "letrec",	LETREC	},
	{ "module",	MODSYM	},	/* for sideways compatability */
	{ "mu",		MU	},
	{ "nonop",	NONOP	},	/* for backward compatability */
	{ "private",	PRIVATE	},
	{ "pubconst",	PUBCONST },	/* for sideways compatability */
	{ "pubfun",	PUBFUN	},	/* for sideways compatability */
	{ "pubtype",	PUBTYPE	},	/* for sideways compatability */
	{ "save",	SAVE	},
	{ "then",	THEN	},
	{ "to",		TO	},
	{ "type",	TYPESYM	},
	{ "typevar",	TYPEVAR	},
	{ "uses",	USES	},
	{ "use",	USES	},	/* for backward compatability */
	{ "where",	WHERE	},
	{ "whererec",	WHEREREC },
	{ "write",	WRITE	},
	{ "|",		'|'	},
	{ "\\",		LAMBDA	},
	{ nullptr, 0}
};

static String	text_rep(int token);
static Bool	scan_string(void);
static Char	charesc(void);
static Char	hexdigit(Char c);
static Char	octdigit(Char c);
static int	lookup(String s);

void
init_lex(void)
{
	Symbol	*p;

	for (p = reserved; p->sym_name != nullptr; p++)
		p->sym_name = newstring(p->sym_name);

	n_or		= text_rep(OR);
	n_valof		= text_rep(VALOF);
	n_is		= text_rep(IS);
	n_eq		= text_rep(DEFEQ);
	n_gives		= text_rep(GIVES);
	n_abstype	= text_rep(ABSTYPE);
	n_data		= text_rep(DATA);
	n_dec		= text_rep(DEC);
	n_infix		= text_rep(INFIXR);
	n_infixr	= text_rep(INFIXR);
	n_type		= text_rep(TYPESYM);
	n_typevar	= text_rep(TYPEVAR);
	n_uses		= text_rep(USES);
	n_else		= text_rep(ELSE);
	n_if		= text_rep(IF);
	n_in		= text_rep(IN);
	n_lambda	= text_rep(LAMBDA);
	n_let		= text_rep(LET);
	n_letrec	= text_rep(LETREC);
	n_mu		= text_rep(MU);
	n_then		= text_rep(THEN);
	n_where		= text_rep(WHERE);
	n_whererec	= text_rep(WHEREREC);

	n_pos	= newstring("pos");
	n_neg	= newstring("neg");
	n_none	= newstring("none");
}

static String
text_rep(int token)
{
	Symbol	*p;

	for (p = reserved; p->sym_name != nullptr; p++)
		if (p->sym_token == token)
			return p->sym_name;
	return "???";
}

int
yylex(void)
{
	Char	c;
	const	Byte	*start;
	const	Byte	*save_pos;

	for(;;) {
		start = inptr;
		c = FetchChar(&inptr);
		if (c == '\0') {
			if (interactive() && recovering()) {
				/*
				 * try to assist syntax error recovery
				 * by inserting a semicolon, so the next
				 * line can start afresh.
				 */
				inptr = start;
				return ';';
			}
			if (! _getline())
				return EOF;
		} else if (IsDigit(c)) {
			/*
			 * Numeric literal (NUMBER):
			 *
			 *	{digit}+(.{digit}+)?([eE][-+]?{digit}+)?
			 *
			 * This allows a subset of IEEE numbers:
			 * - leading signs are not allowed.
			 * - decimal points must be both preceded and
			 *   followed by digits.
			 * - INFINITY and NAN are not allowed.
			 */
			do {
				c = FetchChar(&inptr);
			} while (IsDigit(c));
			if (c == '.') {
				c = FetchChar(&inptr);
				if (IsDigit(c))
					do {
						c = FetchChar(&inptr);
					} while (IsDigit(c));
				else {
					BackChar(&inptr);
					c = '.';
				}
			}
			save_pos = inptr;
			if (c == 'e' || c == 'E') {
				c = FetchChar(&inptr);
				if (c == '+' || c == '-')
					c = FetchChar(&inptr);
				if (IsDigit(c))
					do {
						c = FetchChar(&inptr);
					} while (IsDigit(c));
				else
					inptr = save_pos;
			}
			BackChar(&inptr);
			yylval.numval = atoNUM((const char *)start);
			return NUMBER;
		} else if (IsAlpha(c) || c == '_') {
			/*
			 * Alphanumeric identifier:
			 *
			 *	({letter}|_)({digit}{letter}|_)*'*
			 */
			do {
				c = FetchChar(&inptr);
			} while (IsAlNum(c) || c == '_');
			while (c == '\'')
				c = FetchChar(&inptr);
			BackChar(&inptr);
			return lookup(newnstring((const char *)start,
					(int)(inptr-start)));
		} else if (IsSpace(c))
			;	/* skip white space */
		else if (IsPunct(c)) {
			switch (c) {
			case '!':
				do {
					c = FetchChar(&inptr);
				} while (c != '\0');
				BackChar(&inptr);
                break;
            case '\'':
				c = FetchChar(&inptr);
				if (c == '\\')
					c = charesc();
				yylval.charval = c;
				c = FetchChar(&inptr);
				if (c != '\'') {
					BackChar(&inptr);
					error(LEXERR,
						"non-terminated character");
				} else if (yylval.charval > MaxChar)
					error(LEXERR,
						"illegal character %x",
						yylval.charval);
				else
					return CHAR;
                break;
            case '"':
				if (scan_string())
					return LITERAL;
                break;
            case '(':
            case ')':
            case ',':
            case ';':
            case '[':
            case ']':
				return c;
			default:
				do {
					c = FetchChar(&inptr);
				} while (! IsSpace(c) && IsPunct(c) &&
					 c != '_' &&
					 c != '!' && c != '\'' && c != '"' &&
					 c != '(' && c != ')' && c != ',' &&
					 c != ';' && c != '[' && c != ']');
				BackChar(&inptr);
				return lookup(newnstring((const char *)start,
						(int)(inptr-start)));
			}
		} else
			error(LEXERR, "illegal character %d", c);
	}
}

static Bool
scan_string(void)
{
static	SChar	literal[MAX_STRING+1]; /* space for string constants */
static	Text	lit_text;
	SChar	*lp;
	Char	c;

	lp = literal;
	while ((c = FetchChar(&inptr)) != '"') {
		if (c == '\0') {
			BackChar(&inptr);
			error(LEXERR, "non-terminated string");
			return FALSE;
		}
		if (c == '\\')
			c = charesc();
		if (c > MaxChar)
			error(LEXERR, "illegal character %x", c);
		else if (lp != &literal[MAX_STRING])
			*lp++ = c;
	}
	if (lp == &literal[MAX_STRING])
		error(LEXERR, "string too long");
	*lp = '\0';
	lit_text.t_start = literal;
	lit_text.t_length = (int)(lp-literal);
	yylval.textval = &lit_text;
	return TRUE;
}

/*
 *	Escaped character sequence -- last char was a backslash.
 */
static Char
charesc(void)
{
	Char	c;

	switch (c = FetchChar(&inptr)) {
	case 'a': return '\007';	/* not all C's have \a */
	case 'b': return '\b';
	case 'f': return '\f';
	case 'n': return '\n';
	case 'r': return '\r';
	case 't': return '\t';
	case 'v': return '\013';	/* not all C's have \v */
	case 'x': /* hexadecimal */
		return hexdigit(hexdigit((Char)0));
	case 'X': /* long hexadecimal */
		return hexdigit(hexdigit(hexdigit(hexdigit((Char)0))));
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': /* octal */
		return octdigit(octdigit(c - '0'));
	case '\0':
		/* will be reported as a non-terminated character or string */
		BackChar(&inptr);
		return '\\';
	default:
		return c;
	}
}

static Char
hexdigit(Char c)
{
	Char	d;

	d = FetchChar(&inptr);
	if ('0' <= d && d <= '9')
		return c*16 + d - '0';
	if ('a' <= d && d <= 'f')
		return c*16 + d - 'a' + 10;
	if ('A' <= d && d <= 'F')
		return c*16 + d - 'A' + 10;
	BackChar(&inptr);
	return c;
}

static Char
octdigit(Char c)
{
	Char	d;

	d = FetchChar(&inptr);
	if ('0' <= d && d <= '7')
		return c*8 + d - '0';
	BackChar(&inptr);
	return c;
}

static int
lookup(String s)
{
	Symbol	*p;
	Op	*op;

	for (p = reserved; p->sym_name != nullptr; p++)
		if (p->sym_name == s)
			return p->sym_token;
	yylval.strval = s;
	if ((op = op_lookup(s)) != nullptr)
		/*
		 * Operator tokens: this rests on knowing the order
		 * in which the tokens are generated by Mult-op.awk,
		 * and the fact that BIN_BASE comes immediately before
		 * the first binary token (assured by Mult-op.awk).
		 */
		return (op->op_prec - MINPREC)*NUM_ASSOC +
			(int)(op->op_assoc) + BIN_BASE + 1;
	return IDENT;
}
