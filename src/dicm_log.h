#ifndef DICM_LOG_H
#define DICM_LOG_H

#include "dicm_private.h"

enum log_level_type {
  LOG_TRACE = 0,
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
  LOG_FATAL
};

void _log_msg(enum log_level_type log_level, const char *fmt, ...);

#endif /* DICM_LOG_H */
