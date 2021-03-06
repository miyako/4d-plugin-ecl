/*
 * FEATURES LINKED IN:
 */

/* Locatives								*/
/* #undef LOCATIVE */
/* Use old MIT LOOP macro system					*/
/* #undef ECL_OLD_LOOP */

/* Define this if you want a runtime version only without compiler	*/
/* #undef RUNTIME */
/* Profile tool								*/
/* #undef PROFILE */
/* Program Development Environment					*/
/* #undef PDE */

/* Allow loading dynamically linked code				*/
#define ENABLE_DLOPEN 1

/* Undefine this if you do not want ECL to check for circular lists	*/
#define ECL_SAFE

/* Use CMU Common-Lisp's FORMAT routine					*/
#define ECL_CMU_FORMAT 1

/* Assembler implementation of APPLY and friends			*/
/* #undef ECL_ASM_APPLY */

/* Activate Boehm-Weiser incremental garbage collector			*/
/* #undef GBC_BOEHM_GENGC */

#define ECL_WEAK_HASH

/*
 * SYSTEM FEATURES:
 */

/* Arguments cannot be accessed as array				*/
/* #undef NO_ARGS_ARRAY */
/* Most significant byte first						*/
/* #undef WORDS_BIGENDIAN */
/* Has <sys/resource.h>							*/
/* #undef HAVE_SYS_RESOURCE_H */
/* #undef HAVE_ULIMIT_H */
/* High precision timer							*/
/* #undef HAVE_NANOSLEEP */
/* Float version if isnan()						*/
/* #undef HAVE_ISNANF */
/* float.h for epsilons, maximum real numbers, etc			*/
#define HAVE_FLOAT_H 1
/* select()								*/
/* #undef HAVE_SELECT */
/* #undef HAVE_SYS_SELECT_H */
/* #undef HAVE_SYS_IOCTL_H */
/* putenv() or setenv()							*/
#undef HAVE_SETENV
#define HAVE_PUTENV 1
/* times() and sys/times.h						*/
/* #undef HAVE_TIMES */
/* gettimeofday() and sys/time.h					*/
/* #undef HAVE_GETTIMEOFDAY */
/* getrusage() and sys/resource.h					*/
/* #undef HAVE_GETRUSAGE */
/* user home directory, user name, etc...				*/
/* #undef HAVE_PW_H */
/* symbolic links and checking their existence				*/
/* #undef HAVE_LSTAT */
/* safe creation of temporary files					*/
/* #undef HAVE_MKSTEMP */
/* timer for userland threads						*/
/* #undef HAVE_ALARM */
/* filesytem								*/
/* #undef HAVE_DIRENT_H */
/* dynamic linking of libraries						*/
/* #undef HAVE_DLFCN_H */
/* #undef HAVE_LINK_H */
/* #undef HAVE_MACH_O_DYLD_H */
/* POSIX signals							*/
/* #undef HAVE_SIGPROCMASK */
/* isatty() checks whether a file is connected to a                     */ 
#define HAVE_ISATTY  1 
/* do we want to have signed zeros					*/
#define ECL_SIGNED_ZERO 1
/* has support for large files						*/
/* #undef HAVE_FSEEKO */
/* compiler understands long long                                       */ 
#define HAVE_LONG_LONG 1
/* the tzset() function gets the current time zone			*/
#define HAVE_TZSET 1
/* several floating point functions (ISO C99)				*/
#if 0
#undef HAVE_EXPF
#undef HAVE_LOGF
#undef HAVE_SQRTF
#undef HAVE_COSF
#undef HAVE_SINF
#undef HAVE_TANF
#undef HAVE_SINHF
#undef HAVE_COSHF
#undef HAVE_TANHF
#endif
#define HAVE_FLOORF
#define HAVE_CEILF
#define HAVE_FABSF
#define HAVE_FREXPF
#define HAVE_LDEXPF
#define HAVE_LOG1PL
/* whether we have sched_yield() that gives priority to other threads	*/
/* #undef HAVE_SCHED_YIELD */
/* uname() for system identification					*/
/* #undef HAVE_UNAME */
/* #undef HAVE_UNISTD_H */
/* #undef HAVE_SYS_WAIT_H */
/* size of long long            					*/
#define ECL_LONG_LONG_BITS 64
#define HAVE_SYSTEM

/* what characters are used to mark beginning of new line		*/
#define ECL_NEWLINE_IS_CRLF 1
/* #undef ECL_NEWLINE_IS_LFCR */

/*
 * PARAMETERS:
 */

/*
 * Memory limits for the old garbage collector.
 */
#define LISP_PAGESIZE	2048	/* Page size in bytes			*/
#define MAXPAGE	65536		/* Maximum Memory Size			*/

/* We allocate a number of strings in a pool which is used to speed up reading */
#define ECL_MAX_STRING_POOL_SIZE	10
#define ECL_BUFFER_STRING_SIZE		128

/*
 * Macros that depend on these system features.
 */
#if defined(sparc) || defined(i386) || defined(mips)
#  define	stack_align(n)	(((n) + 0x7) & ~0x7)
#else
#  define	stack_align(n)	(((n) + 03) & ~03)
#endif

#undef FILE_CNT
#if 0 == 1
#  define FILE_CNT(fp)	((fp)->_IO_read_end - (fp)->_IO_read_ptr)
#endif
#if 0 == 2
#  define FILE_CNT(fp)	((fp)->_r)
#endif
#if ( defined(_MSC_VER) && (_MSC_VER < 1900) ) && 3 == 3
#  define FILE_CNT(fp)	((fp)->_cnt)
#endif

#if defined(cygwin) || defined(mingw32) || defined(_MSC_VER)
#  define IS_DIR_SEPARATOR(x) ((x=='/')||(x=='\\'))
#  define DIR_SEPARATOR		'/'
#  define PATH_SEPARATOR	';'
#else
#  define IS_DIR_SEPARATOR(x) (x=='/')
#  define DIR_SEPARATOR	'/'
#  define PATH_SEPARATOR	':'
#endif

#define ECL_ARCHITECTURE "PENTIUM4"

#ifdef ECL_AVOID_FPE_H
# define ecl_detect_fpe()
#else
# include "../../src/c/arch/fpe_none.c"
#endif

#define strcasecmp _stricmp
#if defined(_MSC_VER) && (_MSC_VER < 1800)
#define isnan _isnan
#endif
#define finite _finite
#define sleep _sleep

#include "../../src/c/arch/fpe_none.c"

#include <math.h>
#ifndef isfinite
# define isfinite(x) (finite(x))
# define signbit(x) (_copysign(1.0,(x)) < 0)
# define ECL_MATHERR_CLEAR
# define ECL_MATHERR_TEST
#endif

#if defined(HAVE_LIBFFI) && defined(ECL_INCLUDE_FFI_H)
#include "@ECL_LIBFFI_HEADER@"
#endif

#define ECL_WINDOWS_BACKTRACE 0
#if !ECL_WINDOWS_BACKTRACE
#undef ECL_WINDOWS_BACKTRACE
#endif

#if defined(HAVE_SYS_RESOURCE_H) && defined(RLIMIT_STACK) && !defined(NACL)
#define ECL_DEFAULT_C_STACK_SIZE 0 /* Use the stack size provided by the OS */
#else
#define ECL_DEFAULT_C_STACK_SIZE 1048576
#endif
