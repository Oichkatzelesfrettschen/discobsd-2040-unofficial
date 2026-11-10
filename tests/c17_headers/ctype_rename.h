#ifndef TESTS_C17_HEADERS_CTYPE_RENAME_H
#define TESTS_C17_HEADERS_CTYPE_RENAME_H

/*
 * Link names for the tree's <ctype.h> in a hosted test.  Each declaration
 * binds the standard identifier to a db_ symbol, so the tree's members and
 * its table link beside the hosting C library's without replacing them.
 * The source-level names stay as the tree's header declares them, macros
 * included, which a -D rename would break.
 */
#define	DB_STRING_(text)	#text
#define	DB_STRING(text)		DB_STRING_(text)
#define	DB_LABEL(name) \
	__asm__(DB_STRING(__USER_LABEL_PREFIX__) "db_" #name)

extern const char _ctype_[] DB_LABEL(_ctype_);
int isalnum(int) DB_LABEL(isalnum);
int isalpha(int) DB_LABEL(isalpha);
int isblank(int) DB_LABEL(isblank);
int iscntrl(int) DB_LABEL(iscntrl);
int isdigit(int) DB_LABEL(isdigit);
int isgraph(int) DB_LABEL(isgraph);
int islower(int) DB_LABEL(islower);
int isprint(int) DB_LABEL(isprint);
int ispunct(int) DB_LABEL(ispunct);
int isspace(int) DB_LABEL(isspace);
int isupper(int) DB_LABEL(isupper);
int isxdigit(int) DB_LABEL(isxdigit);
int tolower(int) DB_LABEL(tolower);
int toupper(int) DB_LABEL(toupper);
int isascii(int) DB_LABEL(isascii);
int toascii(int) DB_LABEL(toascii);

#endif /* !TESTS_C17_HEADERS_CTYPE_RENAME_H */
