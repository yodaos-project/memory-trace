#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#include <malloc.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h> // PIPE_BUF
#include <link.h> // _r_debug, link_map

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <new>
#include <algorithm>

#include "list.h"


#define PUBLIC __attribute__ ((visibility("default")))
#define PRIVATE __attribute__ ((visibility("hidden")))

#define ARRAY_SIZE(x) (sizeof (x) / sizeof ((x)[0]))


#define RECORD 1

#define MAX_STACK 32
#define MAX_MODULES 128
#define MAX_SYMBOLS 131071


/* Minimum alignment for this platform */
#ifdef __x86_64__
#define MIN_ALIGN 16
#else
#define MIN_ALIGN (sizeof(double))
#endif


extern "C" void *__libc_malloc(size_t size);
extern "C" void __libc_free(void *ptr);


static void
_assert_fail(const char *expr,
             const char *file,
             unsigned line,
             const char *function)
{
   fprintf(stderr, "%s:%u:%s: Assertion `%s' failed.\n", file, line, function, expr);
   abort();
}


/**
 * glibc's assert macro invokes malloc, so roll our own to avoid recursion.
 */
#ifndef NDEBUG
#define assert(expr) ((expr) ? (void)0 : _assert_fail(#expr, __FILE__, __LINE__, __FUNCTION__))
#else
#define assert(expr) while (0) { (void)(expr) }
#endif


/**
 * Unlike glibc backtrace, libunwind will not invoke malloc.
 */
static int
libunwind_backtrace(unw_context_t *uc, void **buffer, int size)
{
   int count = 0;
   int ret;

   assert(uc != NULL);

   unw_cursor_t cursor;
   ret = unw_init_local(&cursor, uc);
   if (ret != 0) {
      return count;
   }

   while (count < size) {
      unw_word_t ip;
      ret = unw_get_reg(&cursor, UNW_REG_IP, &ip);
      if (ret != 0 || ip == 0) {
         break;
      }

      buffer[count++] = (void *)ip;

      ret = unw_step(&cursor);
      if (ret <= 0) {
         break;
      }
   }

   return count;
}

struct header_t {
   struct list_head list_head;

   // Real pointer
   void *ptr;

   // Size
   size_t size;

   unsigned allocated:1;
   unsigned pending:1;
   unsigned internal:1;

   unsigned char addr_count;
   void *addrs[MAX_STACK];
};


static pthread_mutex_t
mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static ssize_t
total_size = 0;

static ssize_t
max_size = 0;

static ssize_t
limit_size = SSIZE_MAX;

struct list_head
hdr_list = { &hdr_list, &hdr_list };

static inline void
init(struct header_t *hdr,
     size_t size,
     void *ptr,
     unw_context_t *uc)
{
   hdr->ptr = ptr;
   hdr->size = size;
   hdr->allocated = true;
   hdr->pending = false;
   hdr->internal = false;

   if (RECORD) {
      hdr->addr_count = libunwind_backtrace(uc, hdr->addrs, ARRAY_SIZE(hdr->addrs));
   }
}

static inline void
_log(struct header_t *hdr) {
}

static void
_flush(void) {
   struct header_t *it;
   struct header_t *tmp;
   for (it = (struct header_t *)hdr_list.next,
	     tmp = (struct header_t *)it->list_head.next;
        &it->list_head != &hdr_list;
	     it = tmp, tmp = (struct header_t *)tmp->list_head.next) {
      assert(it->pending);
      fprintf(stderr, "flush %p %zu\n", &it[1], it->size);
      if (!it->internal) {
         _log(it);
      }
      list_del(&it->list_head);
      if (!it->allocated) {
         __libc_free(it->ptr);
      } else {
         it->pending = false;
      }
   }
}


/**
 * Update/log changes to memory allocations.
 */
static inline void
_update(struct header_t *hdr,
        bool allocating = true)
{
   pthread_mutex_lock(&mutex);

   static int recursion = 0;

   if (recursion++ <= 0) {
      if (!allocating && max_size == total_size) {
         _flush();
      }

      hdr->allocated = allocating;
      ssize_t size = allocating ? (ssize_t)hdr->size : -(ssize_t)hdr->size;

      if (hdr->pending) {
         assert(!allocating);
         hdr->pending = false;
         list_del(&hdr->list_head);
         __libc_free(hdr->ptr);
      } else {
         hdr->pending = true;
         list_add(&hdr->list_head, &hdr_list);
      }

      if (size > 0 &&
          (total_size + size < total_size || // overflow
           total_size + size > limit_size)) {
         fprintf(stderr, "memtrail: warning: out of memory\n");
         _flush();
         _exit(1);
      }

      total_size += size;
      assert(total_size >= 0);

      if (total_size >= max_size) {
         max_size = total_size;
      }
   } else {
      fprintf(stderr, "memtrail: warning: recursion\n");
      hdr->internal = true;

      assert(!hdr->pending);
      if (!hdr->pending) {
         if (!allocating) {
            __libc_free(hdr->ptr);
         }
      }
   }
   --recursion;

   pthread_mutex_unlock(&mutex);
}


static void *
_memalign(size_t alignment, size_t size, unw_context_t *uc)
{
   void *ptr;
   struct header_t *hdr;
   void *res;

   if ((alignment & (alignment - 1)) != 0 ||
       (alignment & (sizeof(void*) - 1)) != 0) {
      return NULL;
   }

   if (size == 0) {
      // Honour malloc(0), but allocate one byte for accounting purposes.
      ++size;
   }

   ptr = __libc_malloc(alignment + sizeof *hdr + size);
   if (!ptr) {
      return NULL;
   }

   hdr = (struct header_t *)((((size_t)ptr + sizeof *hdr + alignment - 1) & ~(alignment - 1)) - sizeof *hdr);

   init(hdr, size, ptr, uc);
   res = &hdr[1];
   assert(((size_t)res & (alignment - 1)) == 0);
   fprintf(stderr, "alloc %p %zu\n", res, size);

   _update(hdr);

   return res;
}


static inline void *
_malloc(size_t size, unw_context_t *uc)
{
   return _memalign(MIN_ALIGN, size, uc);
}

static void
_free(void *ptr)
{
   struct header_t *hdr;

   if (!ptr) {
      return;
   }

   hdr = (struct header_t *)ptr - 1;

   _update(hdr, false);

   fprintf(stderr, "free %p %zu\n", ptr, hdr->size);
}


/*
 * C
 */

extern "C"
PUBLIC int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
   *memptr = NULL;

   if ((alignment & (alignment - 1)) != 0 ||
       (alignment & (sizeof(void*) - 1)) != 0) {
      return EINVAL;
   }

   unw_context_t uc;
   unw_getcontext(&uc);
   *memptr =  _memalign(alignment, size, &uc);
   if (!*memptr) {
      return -ENOMEM;
   }

   return 0;
}

extern "C"
PUBLIC void *
memalign(size_t alignment, size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(alignment, size, &uc);
}

extern "C"
PUBLIC void *
valloc(size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(sysconf(_SC_PAGESIZE), size, &uc);
}

extern "C"
PUBLIC void *
malloc(size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}

extern "C"
PUBLIC void
free(void *ptr)
{
   _free(ptr);
}


extern "C"
PUBLIC void *
calloc(size_t nmemb, size_t size)
{
   void *ptr;
   unw_context_t uc;
   unw_getcontext(&uc);
   ptr = _malloc(nmemb * size, &uc);
   if (ptr) {
      memset(ptr, 0, nmemb * size);
   }
   return ptr;
}


extern "C"
PUBLIC void
cfree(void *ptr)
{
   _free(ptr);
}


extern "C"
PUBLIC void *
realloc(void *ptr, size_t size)
{
   struct header_t *hdr;
   void *new_ptr;

   unw_context_t uc;
   unw_getcontext(&uc);

   if (!ptr) {
      return _malloc(size, &uc);
   }

   if (!size) {
      _free(ptr);
      return NULL;
   }

   hdr = (struct header_t *)ptr - 1;

   new_ptr = _malloc(size, &uc);
   if (new_ptr) {
      size_t min_size = hdr->size >= size ? size : hdr->size;
      memcpy(new_ptr, ptr, min_size);
      _free(ptr);
   }

   return new_ptr;
}


extern "C"
PUBLIC char *
strdup(const char *s)
{
   size_t size = strlen(s) + 1;
   unw_context_t uc;
   unw_getcontext(&uc);
   char *ptr = (char *)_malloc(size, &uc);
   if (ptr) {
      memcpy(ptr, s, size);
   }
   return ptr;
}


extern "C"
PUBLIC char *
strndup(const char *s, size_t n)
{
   size_t len = 0;
   while (n && s[len]) {
      ++len;
      --n;
   }

   unw_context_t uc;
   unw_getcontext(&uc);
   char *ptr = (char *)_malloc(len + 1, &uc);
   if (ptr) {
      memcpy(ptr, s, len);
      ptr[len] = 0;
   }
   return ptr;
}


static int
_vasprintf(char **strp, const char *fmt, va_list ap, unw_context_t *uc)
{
   size_t size;

   {
      va_list ap_copy;
      va_copy(ap_copy, ap);

      char junk;
      size = vsnprintf(&junk, 1, fmt, ap_copy);
      assert(size >= 0);

      va_end(ap_copy);
   }

   *strp = (char *)_malloc(size, uc);
   if (!*strp) {
      return -1;
   }

   return vsnprintf(*strp, size, fmt, ap);
}

extern "C"
PUBLIC int
vasprintf(char **strp, const char *fmt, va_list ap)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _vasprintf(strp, fmt, ap, &uc);
}

extern "C"
PUBLIC int
asprintf(char **strp, const char *format, ...)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   int res;
   va_list ap;
   va_start(ap, format);
   res = _vasprintf(strp, format, ap, &uc);
   va_end(ap);
   return res;
}


/*
 * C++
 */

PUBLIC void *
operator new(size_t size) noexcept(false) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void *
operator new[] (size_t size) noexcept(false) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void
operator delete (void *ptr) noexcept {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr) noexcept {
   _free(ptr);
}


PUBLIC void *
operator new(size_t size, const std::nothrow_t&) noexcept {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void *
operator new[] (size_t size, const std::nothrow_t&) noexcept {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void
operator delete (void *ptr, const std::nothrow_t&) noexcept {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr, const std::nothrow_t&) noexcept {
   _free(ptr);
}


/*
 * Snapshot.
 */


static size_t last_snapshot_size = 0;
static unsigned snapshot_no = 0;

extern "C"
PUBLIC void
memtrail_snapshot(void) {
   pthread_mutex_lock(&mutex);

   _flush();

   size_t current_total_size = total_size;
   size_t current_delta_size;
   if (snapshot_no)
      current_delta_size = current_total_size - last_snapshot_size;
   else
      current_delta_size = 0;
   last_snapshot_size = current_total_size;

   ++snapshot_no;

   pthread_mutex_unlock(&mutex);

   fprintf(stderr, "memtrail: snapshot %zi bytes (%+zi bytes)\n", current_total_size, current_delta_size);
}

/*
 * Constructor/destructor
 */


__attribute__ ((constructor(101)))
static void
on_start(void)
{
   printf("start trace\n");
   // Only trace the current process.
   unsetenv("LD_PRELOAD");

   // Abort when the application allocates half of the physical memory, to
   // prevent the system from slowing down to a halt due to swapping
   long pagesize = sysconf(_SC_PAGESIZE);
   long phys_pages = sysconf(_SC_PHYS_PAGES);
   limit_size = (ssize_t) std::min((intmax_t) phys_pages / 2, SSIZE_MAX / pagesize) * pagesize;
   fprintf(stderr, "memtrail: limiting to %zi bytes\n", limit_size);
}


__attribute__ ((destructor(101)))
static void
on_exit(void)
{
   printf("exit trace\n");
   pthread_mutex_lock(&mutex);
   _flush();
   size_t current_max_size = max_size;
   size_t current_total_size = total_size;
   pthread_mutex_unlock(&mutex);

   fprintf(stderr, "memtrail: maximum %zi bytes\n", current_max_size);
   fprintf(stderr, "memtrail: leaked %zi bytes\n", current_total_size);

   // We don't close the fd here, just in case another destructor that deals
   // with memory gets called after us.
}


// vim:set sw=3 ts=3 et:

