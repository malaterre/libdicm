#include "dicm.h"

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* EXIT_SUCCESS */

static const char *events[] = {
    "stream-start", "stream-end",     "document-start", "document-end",
    "key",          "fragment",       "value",          "item-start",
    "item-end",     "sequence-start", "sequence-end"};

static struct log_count {
  unsigned int trace;
  unsigned int debug;
  unsigned int info;
  unsigned int warn;
  unsigned int error;
  unsigned int fatal;
} log_count;

static void my_log(int log_level, const char *msg) {
  // fprintf(stderr, "LOG: %d - %s\n", log_level, msg);
  switch (log_level) {
  case DICM_LOG_TRACE:
    log_count.trace++;
    break;
  case DICM_LOG_DEBUG:
    log_count.debug++;
    break;
  case DICM_LOG_INFO:
    log_count.info++;
    break;
  case DICM_LOG_WARN:
    log_count.warn++;
    break;
  case DICM_LOG_ERROR:
    log_count.error++;
    break;
  case DICM_LOG_FATAL:
    log_count.fatal++;
    break;
  }
}

int parsing(int argc, char *argv[]) {
  struct dicm_parser *parser;
  struct dicm_src *src;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const size_t buflen = sizeof buf;
  FILE *in = fopen(argv[1], "rb");

  dicm_configure_log_msg(my_log);

  dicm_src_file_create(&src, in);
  FILE *out = fopen(argv[2], "w");

  if (dicm_parser_create(&parser) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "parser\n");
    exit(1);
  }

  dicm_parser_set_input(parser, src);

  /* Read the event sequence. */
  while (!done) {
    const int next = dicm_parser_next_event(parser);
    if (next < 0) {
      goto error;
    }
    /* Get the next event. */
    enum dicm_event_type etype = next;
    fprintf(out, "%s", events[etype]);

    /*
    ...
    Process the event.
    ...
    */
    switch (etype) {
    case DICM_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      fprintf(out, " %08x %s", key.tag, &key.vr);
      break;
    case DICM_VALUE_EVENT:
      res = dicm_parser_get_value_length(parser, &size);
      assert(res == 0);
      /* do/while loop trigger at least one event (even in the case where
       * value_length is exactly 0) */
      const size_t len = size < buflen ? size : buflen;
      do {
        res = dicm_parser_read_value(parser, buf, len);
        assert(res == 0);
        size -= len;
      } while (size != 0);
      fprintf(out, " %.*s", (int)len, buf);
      break;
    default:;
    }

    /* Are we finished? */
    done = (etype == DICM_STREAM_END_EVENT);
    fprintf(out, "\n");
  }

  /* Destroy the Parser object. */
  dicm_delete(parser);
  dicm_delete(src);
  fclose(in);
  fclose(out);
  if (log_count.error != 0) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;

error:
  dicm_delete(parser);
  dicm_delete(src);
  fclose(in);
  fclose(out);
  return EXIT_FAILURE;
}
