#ifndef REVK_CTYPE_H
#define REVK_CTYPE_H

#include <ctype.h>
#define	is_digit(x)	isdigit((unsigned char)(x))
#define	is_xdigit(x)	isxdigit((unsigned char)(x))
#define	is_alpha(x)	isalpha((unsigned char)(x))
#define	is_alnum(x)	isalnum((unsigned char)(x))
#define	is_space(x)	isspace((unsigned char)(x))

#endif
