//
// Created by ximin.chen@rokid.com on 2018/12/17.
//

#include "memory-trace.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define fast_malloc(var, T, count) T *var = (T *) malloc(sizeof(T) * count)
#define fast_free(var) free(var); var = NULL;

typedef struct foo_s {
  int a1;
  char *a2;
  double a3;
  float a4;
} foot_t;

void bar1() {
  fast_malloc(p1, char, 12);
}
void foo1() {
  bar1();
}

void foo2() {
  fast_malloc(p1, int, 12);
}

void rab3() {
  fast_malloc(p1, foot_t, 1);
}

void bar3() {
  rab3();
}
void foo3() {
  bar3();
}

int main(int argc, char **argv) {
  fast_malloc(p1, char, 12);
  fast_malloc(p2, int, 2);
  fast_malloc(p3, double, 3);
  fast_malloc(p4, float, 5);
  *p2 = 12;
  *(p3 + 1) = 4.0;
  *(p4 + 2) = 5.0f;
  printf("p2: %d, p3: %f, p4: %fi\n", *p2, *(p3 + 1), *(p4 + 2));
  print_trace();
  fast_free(p2);
  fast_free(p3);
  fast_free(p4);
  print_trace();
  free(p1);
  print_trace();
  foo1();
  print_trace();
  foo2();
  print_trace();
  foo3();
  int r = dump_trace_json("data.json");
  assert(r == 0);
  
  return 0;
}