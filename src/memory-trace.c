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
#include <errno.h>

#define LOG_INFO(...) fprintf(stdout, __VA_ARGS__)
#define LOG_ERROR(...) fprintf(stderr, __VA_ARGS__)

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
  uint32_t trace_size;
} memory_record_t;

static int record_cmp(void *v1, void *v2) {
  void *memory1 = v1;
  void *memory2 = ((memory_record_t *) v2)->memory;
  return memory1 == memory2 ? 0 : 1;
}

#define BACKTRACE_DEEP 10
#define JSON_OBJ_BUF_MAX 16 * 1024
#define PROPERTY_BUF_MAX 512

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

void yoda_free(void *p) {
  if (p == NULL) {
    return;
  }
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

void print_trace() {
  size_t total = 0;
  pthread_mutex_lock(&g_acquire_malloc_lock);
  LOG_INFO("-----------------------------------\n");
  LIST_FOREACH(&g_trace_list, node) {
    memory_record_t *record = NODE_DATA(node, memory_record_t);
    total += record->size;
    LOG_INFO("%s: %dL, %ld bytes\n",
      record->filename, record->line, record->size);
    // skip record_list and yoda_malloc backtrace
    for (uint32_t i = 2; i < record->trace_size; ++i) {
      LOG_INFO("%s\n", ++record->backtrace[i]);
    }
  }
  pthread_mutex_unlock(&g_acquire_malloc_lock);
  LOG_INFO("total: %ld bytes\n", total);
}

static void do_flush(FILE *file, char *buf, char **buf_start) {
  size_t buf_size = *buf_start - buf;
  if (buf_size > 0) {
    fwrite(buf, buf_size, 1, file);
    *buf_start = buf;
  }
}

static int flush_buf(FILE *file, char *buf, char **buf_start,
                     char *buf_end, const char *src, int force) {
  size_t src_len = strlen(src);
  size_t buf_left_len = buf_end - *buf_start;
  if (buf_left_len < src_len) {
    if (*buf_start == buf) {
      LOG_ERROR("write src is too long before flush: %ld", src_len);
      return 2;
    }
    do_flush(file, buf, buf_start);
    buf_left_len = buf_end - *buf_start;
    if (buf_left_len < src_len) {
      LOG_ERROR("write src is too long after flush: %ld", src_len);
      return 3;
    }
  }
  if (force) {
    do_flush(file, buf, buf_start);
    fwrite(src, src_len, 1, file);
  } else {
    memcpy(*buf_start, src, src_len);
    *buf_start += src_len;
  }
  #undef DO_WRITE
  return 0;
}

int dump_trace_json(const char *filename) {
  FILE *file = fopen(filename, "w");
  if (!file) {
    char *err = strerror(errno);
    LOG_ERROR("open %s error: %s", filename, err);
    return errno;
  }
  char *obj_buf = (char *) malloc(JSON_OBJ_BUF_MAX);
  char *buf_start = obj_buf;
  char *buf_end = obj_buf + JSON_OBJ_BUF_MAX - 1;
  char property_buf[PROPERTY_BUF_MAX];
  int r = 0;

  #define WRITE_PROPERTY_BUF(format, value)                                    \
    snprintf(property_buf, PROPERTY_BUF_MAX, format, value);                   \
    r = flush_buf(file, obj_buf, &buf_start, buf_end, property_buf, 0);        \
    if (r != 0) {                                                              \
      break;                                                                   \
    }

  pthread_mutex_lock(&g_acquire_malloc_lock);
  fwrite("[", 1, 1, file);
  LIST_FOREACH(&g_trace_list, node) {
    memory_record_t *record = NODE_DATA(node, memory_record_t);
    WRITE_PROPERTY_BUF("{\"filename:\":\"%s\",", record->filename);
    WRITE_PROPERTY_BUF("\"line:\":%d,", record->line);
    WRITE_PROPERTY_BUF("\"size:\":%ld,", record->size);
    WRITE_PROPERTY_BUF("\"backtrace:\":[%s", "");
    for (uint32_t i = 2; i < record->trace_size; ++i) {
      if (i < record->trace_size -1) {
        WRITE_PROPERTY_BUF("\"%s\",", ++record->backtrace[i]);
      } else {
        if (node->next) {
          WRITE_PROPERTY_BUF("\"%s\"]},", ++record->backtrace[i]);
        } else {
          WRITE_PROPERTY_BUF("\"%s\"]}", ++record->backtrace[i]);
        }
      }
    }
  }
  #undef SPRINTF_PROPERTY
  if (r == 0) {
    flush_buf(file, obj_buf, &buf_start, buf_end, "]", 1);
    fclose(file);
  } else {
    LOG_ERROR("write json error code: %d", r);
    fclose(file);
    unlink(filename);
  }
  free(obj_buf);
  pthread_mutex_unlock(&g_acquire_malloc_lock);
  return r;
}
