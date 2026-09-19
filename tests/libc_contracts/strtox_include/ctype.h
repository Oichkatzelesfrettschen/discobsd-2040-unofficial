#ifndef TESTS_LIBC_CONTRACTS_STRTOX_CTYPE_H
#define TESTS_LIBC_CONTRACTS_STRTOX_CTYPE_H

int test_isalpha(int);
int test_isdigit(int);
int test_isspace(int);
int test_isupper(int);

#define isalpha(character) test_isalpha(character)
#define isdigit(character) test_isdigit(character)
#define isspace(character) test_isspace(character)
#define isupper(character) test_isupper(character)

#endif
