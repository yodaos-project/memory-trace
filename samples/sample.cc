//
// Created by ximin.chen@rokid.com on 2018/12/17.
//

// #include "memory-trace.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <stdlib.h>

struct foot_t {
  int a1;
  char *a2;
  double a3;
  float a4;
};

void bar1() {
  char *p1 = new char[12];
}
void foo1() {
  bar1();
}

void foo2() {
  int *p1 = new int[12];
}

void rab3() {
  foot_t *p1 = new foot_t;
}

void bar3() {
  rab3();
}
void foo3() {
  bar3();
}

int main(int argc, char **argv) {
  foot_t *p0 = new foot_t;
  char *p1 = new char[12];
  int *p2 = new int[2];
  double *p3 = new double[3];
  float *p4 = new float[5];
  *p2 = 12;
  *(p3 + 1) = 4.0;
  *(p4 + 2) = 5.0f;
  printf("p2: %d, p3: %f, p4: %fi\n", *p2, *(p3 + 1), *(p4 + 2));
  // print_trace();
  delete[] p2;
  delete[] p3;
  delete[] p4;
  // print_trace();
  delete[] p1;
  free(p0);
  // print_trace();
  foo1();
  // print_trace();
  foo2();
  // print_trace();
  foo3();
  // int r = dump_trace_json("data.json");
  
  return 0;
}