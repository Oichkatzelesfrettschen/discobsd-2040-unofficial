/*
 * Feature selectors precede every public header.  Compiler dialect alone
 * leaves the default BSD namespace intact.  Internal levels describe header
 * visibility, rather than a claim that every interface is implemented.
 */
#ifndef _DISCOBSD_FEATURETEST_H_
#define _DISCOBSD_FEATURETEST_H_

#if defined(_XOPEN_SOURCE)
#define _DISCOBSD_XSI_VISIBLE _XOPEN_SOURCE
#else
#define _DISCOBSD_XSI_VISIBLE 0
#endif

#if _DISCOBSD_XSI_VISIBLE >= 800
#define _DISCOBSD_XSI_POSIX 202405L
#elif _DISCOBSD_XSI_VISIBLE >= 700
#define _DISCOBSD_XSI_POSIX 200809L
#elif _DISCOBSD_XSI_VISIBLE >= 600
#define _DISCOBSD_XSI_POSIX 200112L
#elif _DISCOBSD_XSI_VISIBLE >= 500
#define _DISCOBSD_XSI_POSIX 199506L
#else
#define _DISCOBSD_XSI_POSIX 0L
#endif

#if defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE > _DISCOBSD_XSI_POSIX
#define _DISCOBSD_POSIX_VISIBLE _POSIX_C_SOURCE
#else
#define _DISCOBSD_POSIX_VISIBLE _DISCOBSD_XSI_POSIX
#endif
#elif defined(_ANSI_SOURCE)
#define _DISCOBSD_POSIX_VISIBLE 0L
#elif defined(_POSIX_SOURCE)
#define _DISCOBSD_POSIX_VISIBLE 199009L
#else
#define _DISCOBSD_POSIX_VISIBLE 202405L
#endif

#if defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
#define _DISCOBSD_BSD_VISIBLE 1
#elif defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || \
    defined(_ANSI_SOURCE) || defined(_POSIX_SOURCE)
#define _DISCOBSD_BSD_VISIBLE 0
#else
#define _DISCOBSD_BSD_VISIBLE 1
#endif

#endif /* _DISCOBSD_FEATURETEST_H_ */
