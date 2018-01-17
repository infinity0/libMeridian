#ifndef COMMON_HEADER
#define COMMON_HEADER

#define ERROR_STREAM	stderr
#define WARN_STREAM		stdout

#define ERROR_LOG(a)						fprintf(ERROR_STREAM, a)
#define ERROR_LOG_1(a,b)					fprintf(ERROR_STREAM, a, b)
#define ERROR_LOG_2(a,b,c)					fprintf(ERROR_STREAM, a, b, c)
#define ERROR_LOG_3(a,b,c,d)				fprintf(ERROR_STREAM, a, b, c, d)
#define ERROR_LOG_4(a,b,c,d,e)				fprintf(ERROR_STREAM, a, b, c, d, e)

#define DSL_ERROR(format, args...)  \
		fprintf(WARN_STREAM, format, ## args)

#ifdef DEBUG

#define WARN_LOG(a)							fprintf(WARN_STREAM, a)
#define WARN_LOG_1(a,b)						fprintf(WARN_STREAM, a, b)
#define WARN_LOG_2(a,b,c)					fprintf(WARN_STREAM, a, b, c)
#define WARN_LOG_3(a,b,c,d)					fprintf(WARN_STREAM, a, b, c, d)
#define WARN_LOG_4(a,b,c,d,e)				fprintf(WARN_STREAM, a, b, c, d, e)

#define ASSERT(tst, msg)	(tst ? (void)0 : (fprintf(stderr, \
	"%s, Failed in file %s at line %d\n", msg, __FILE__, __LINE__), abort()))
#else

#define WARN_LOG(a)							(void)0
#define WARN_LOG_1(a,b)						(void)0
#define WARN_LOG_2(a,b,c)					(void)0
#define WARN_LOG_3(a,b,c,d)					(void)0
#define WARN_LOG_4(a,b,c,d,e)				(void)0

#define ASSERT(tst, msg)					(void)0

#endif
#endif
