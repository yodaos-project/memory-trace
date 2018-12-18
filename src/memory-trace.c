//
// Created by ximin.chen@rokid.com on 2018/12/17.
//

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <execinfo.h>

typedef struct list_node_s {
  struct list_node_s *prev;
  struct list_node_s *next;
  void *data;
} list_node_t;

typedef struct list_s {
  list_node_t *head;
  list_node_t *tail;
  size_t length;
} list_t;

static list_t *list_create() {
  list_t *self = (list_t *) malloc(sizeof(list_t));
  self->head = NULL;
  self->tail = NULL;
  self->length = 0;
  return self;
}

static list_node_t *list_node_create(void *data) {
  list_node_t *node = (list_node_t *) malloc(sizeof(list_node_t));
  node->prev = NULL;
  node->next = NULL;
  node->data = data;
  return node;
}

static void list_destroy(list_t *list) {
  unsigned int len = list->length;
  list_node_t *next;
  list_node_t *node = list->head;

  while (len--) {
    next = node->next;
    free(node->data);
    free(node);
    node = next;
  }

  free(list);
}

static list_node_t *list_push_back(list_t *list, list_node_t *node) {
  assert(node);
  if (list->length) {
    node->prev = list->tail;
    node->next = NULL;
    list->tail->next = node;
    list->tail = node;
  } else {
    list->head = list->tail = node;
    node->prev = node->next = NULL;
  }

  ++list->length;
  return node;
}

static list_node_t *list_pop_back(list_t *list) {
  if (list->length < 1) {
    return NULL;
  }

  list_node_t *node = list->tail;

  if (--list->length) {
    (list->tail = node->prev)->next = NULL;
  } else {
    list->tail = list->head = NULL;
  }

  node->next = node->prev = NULL;
  return node;
}

static void list_remove(list_t *list, list_node_t *node) {
  node->prev ? (node->prev->next = node->next) : (list->head = node->next);

  node->next ? (node->next->prev = node->prev) : (list->tail = node->prev);

  free(node->data);
  free(node);
  --list->length;
}

#define LIST_FOREACH(list, var) \
  for (list_node_t *var = (list)->head; var != NULL; var = var->next)

#define NODE_DATA(node, T) ((T *) node->data)

typedef int (*list_cmp_func)(void *v1, void *v2);

static list_node_t* list_find(list_t *list, void *data, list_cmp_func func) {
  LIST_FOREACH(list, node) {
    if (func) {
      if (func(data, node->data) == 0) {
        return node;
      }
    } else if (data == node->data) {
      return node;
    }
  }
  return NULL;
}

typedef struct memory_record_s {
  char *filename;
  uint32_t line;
  size_t size;
  void *memory; // weak pointer
  char **backtrace;
  int trace_size;
} memory_record_t;

int record_cmp(void *v1, void *v2) {
  void *memory1 = v1;
  void *memory2 = ((memory_record_t *) v2)->memory;
  return memory1 == memory2 ? 0 : 1;
}

#define BACKTRACE_DEEP 10

static list_t g_trace_list;
static pthread_mutex_t g_acquire_malloc_lock = PTHREAD_MUTEX_INITIALIZER;

static void record_list(size_t size, char *filename, int line, void *memory) {
  pthread_mutex_lock(&g_acquire_malloc_lock);
  size_t filename_length = strlen(filename);
  memory_record_t *record = (memory_record_t *) malloc(sizeof(memory_record_t));
  record->memory = memory;
  record->size = size;
  record->line = line;
  record->filename = (char *) malloc(filename_length + 1);
  memcpy(record->filename, filename, filename_length);
  record->filename[filename_length] = '\0';
  void *trace[BACKTRACE_DEEP];
  record->trace_size = backtrace(trace, BACKTRACE_DEEP);
  record->backtrace = backtrace_symbols(trace, record->trace_size);

  list_node_t* record_node = list_node_create(record);
  list_push_back(&g_trace_list, record_node);
  pthread_mutex_unlock(&g_acquire_malloc_lock);
}

void *yoda_malloc(size_t size, char *filename, int line) {
  void *memory = malloc(size);

  if (NULL != memory) {
    record_list(size, filename, line, memory);
  }

  return memory;
}

void *yoda_calloc(size_t number, size_t size, char *filename, int line) {
  void *memory = calloc(number, size);
  if (memory != NULL) {
    record_list(size * number, filename, line, memory);
  }
  return memory;
}

void *yoda_realloc(void *ptr, size_t size, char *filename, int line) {
  void *memory = realloc(ptr, size);
  if (memory != NULL) {
    record_list(size, filename, line, memory);
  }
  return memory;
}

void yoda_basic_free(void *p) {
  pthread_mutex_lock(&g_acquire_malloc_lock);

  list_node_t *node = list_find(&g_trace_list, p, record_cmp);
  if (node != NULL) {
    memory_record_t *record = NODE_DATA(node, memory_record_t);
    free(record->filename);
    free(record->backtrace);
    list_remove(&g_trace_list, node);
  }
  pthread_mutex_unlock(&g_acquire_malloc_lock);
  free(p);
}

void yoda_free(void *p) {
  if (p != NULL) {
    yoda_basic_free(p);
  }
}

void print_trace() {
  pthread_mutex_lock(&g_acquire_malloc_lock);
  size_t total = 0;
  printf("-----------------------------------\n");
  LIST_FOREACH(&g_trace_list, node) {
    memory_record_t *record = NODE_DATA(node, memory_record_t);
    total += record->size;
    printf("%s: %dL, %ld bytes\n", record->filename, record->line, record->size);
    // skip record_list and yoda_malloc backtrace
    for (uint32_t i = 2; i < record->trace_size; ++i) {
      printf("%d%s\n", i - 2, ++record->backtrace[i]);
    }
  }
  printf("total: %ldbytes\n", total);
  pthread_mutex_unlock(&g_acquire_malloc_lock);
}

void dump_trace_json(const char *filepath) {
  pthread_mutex_lock(&g_acquire_malloc_lock);
  pthread_mutex_unlock(&g_acquire_malloc_lock);
}
