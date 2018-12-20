//
// Created by ximin.chen@rokid.com on 2018/12/17.
//

#include <stdlib.h>
#include <stddef.h>

void print_trace();
int dump_trace_json(const char *filename);

#ifdef __cplusplus
// #include <new>

// void *operator new(size_t size) throw(std::bad_alloc) {
//   return malloc(size);
// }

// void *operator new[](size_t size) throw(std::bad_alloc) {
//   return operator new(size);
// }

// void operator delete(void *p) throw() {
//   free(p);
// }

// void operator delete[](void *p) throw(){
//   operator delete(p);
// }

#endif
