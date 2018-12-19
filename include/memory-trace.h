//
// Created by ximin.chen@rokid.com on 2018/12/17.
//

#include <stdlib.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* yoda_malloc(size_t length, char *filename, int line);
void* yoda_calloc(size_t number, size_t length, char *filename, int line);
void* yoda_realloc(void *ptr, size_t length, char *filename, int line);
void yoda_free(void *ptr_temp);
void print_trace();
int dump_trace_json(const char *filename);

#define malloc(x) yoda_malloc((x), __FILE__, __LINE__)
#define free(x) yoda_free((x))
#define calloc(x,y) yoda_calloc((x),(y), __FILE__, __LINE__)
#define realloc(x,y) yoda_realloc((x), (y), __FILE__, __LINE__)

#ifdef __cplusplus
}

void *operator new[](size_t size, char *file, int line);

void *operator new(size_t size, char *file, int line);

void operator delete[](void *p);

void operator delete(void *p);

#define new[] new[](__FILE__, __LINE__)

#define new new(__FILE__, __LINE__)

#endif