#include "dicm_log.h"

#include <stdarg.h>    /* va_list */
#include <stdatomic.h> /* _Atomic */
#include <stdio.h>     /* fprintf */
#include <string.h>    /* strlen */

struct logging {
  void (*fp_msg)(int, const char *) DICM_NONNULL();
};

static void _default_log_msg(int, const char *) DICM_NONNULL();

static _Atomic struct logging global_log = {
    /* log interface */
    .fp_msg = _default_log_msg};

static const char *log_level_str[] = {"trace", "debug", "info",
                                      "warn",  "error", "fatal"};

void _default_log_msg(int log_level, const char *msg) {
  assert(log_level >= DICM_LOG_TRACE && log_level <= DICM_LOG_FATAL);
  assert(msg);
  assert(strlen(msg) < 512);
  fprintf(stderr, "%s: %s\n", log_level_str[log_level], msg);
}

void _log_msg(enum dicm_log_level_type log_level, const char *fmt, ...) {
  char buffer[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, ap);
  va_end(ap);
#if 0
  global_log.fp_msg(log_level, buffer);
#else
  struct logging tmp = atomic_load_explicit(&global_log, memory_order_relaxed);
  tmp.fp_msg(log_level, buffer);
#endif
}

void dicm_configure_log_msg(void (*fp_msg)(int, const char *)) {
#if 0
  global_log.fp_msg = fp_msg;
#else
  struct logging tmp = {.fp_msg = fp_msg};
  atomic_store_explicit(&global_log, tmp, memory_order_relaxed);
#endif
}
