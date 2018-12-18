//
// Created by ximin.chen@rokid.com on 2018/12/17.
//

#include "memory-trace.c"

void *operator new(size_t size, char *filename, int line) {
  return yoda_malloc(size, filename, line);
}

void *operator new[](size_t size, char *filename, int line) {
  return yoda_malloc(size, filename, line);
}

void operator delete[](void *p) {
  yoda_free(p);
  return;
}

void operator delete(void *p) {
  yoda_free(p);
  return;
}