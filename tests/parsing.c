#include "dicm.h"

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strcmp */

static const char *events[] = {
    "document-start", "document-end",   "key",
    "fragment",       "value",          "item-start",
    "item-end",       "sequence-start", "sequence-end"};

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
  uint32_t size, oldsize;
  int res;
  const size_t buflen = sizeof buf;
  const char *structure = argv[1];
  const char *infilename = argv[2];
  const char *outfilename = argv[3];
  FILE *in = fopen(infilename, "rb");

  dicm_configure_log_msg(my_log);

  dicm_src_file_create(&src, in);
  FILE *out = fopen(outfilename, "w");

  if (dicm_parser_create(&parser) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "parser\n");
    exit(1);
  }

  if (structure == NULL || strcmp("evrle_encapsulated", structure) == 0) {
    dicm_parser_set_input(parser, DICM_STRUCTURE_ENCAPSULATED, src);
  } else if (strcmp("ivrle_raw", structure) == 0) {
    dicm_parser_set_input(parser, DICM_STRUCTURE_IMPLICIT, src);
  } else if (strcmp("evrle_raw", structure) == 0) {
    dicm_parser_set_input(parser, DICM_STRUCTURE_EXPLICIT_LE, src);
  } else if (strcmp("evrbe_raw", structure) == 0) {
    dicm_parser_set_input(parser, DICM_STRUCTURE_EXPLICIT_BE, src);
  } else {
    fprintf(stderr, "Invalid structure: %s\n", structure);
    exit(1);
  }

  /* Read the event sequence. */
  while (!done) {
    const int next = dicm_parser_next_event(parser);
    if (next < 0) {
      goto error;
    }
    /* Get the next event. */
    enum dicm_event_type etype = next;

    /*
    ...
    Process the event.
    ...
    */
    switch (etype) {
    case DICM_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      fprintf(out, "%s", events[etype]);
      if (key.vr != 0) {
        fprintf(out, " %08x %s", key.tag, (char *)&key.vr);
      } else {
        fprintf(out, " %08x", key.tag);
      }
      break;
    case DICM_VALUE_EVENT:
      res = dicm_parser_get_size(parser, &size);
      oldsize = size;
      assert(res == 0);
      /* do/while loop trigger at least one event (even in the case where
       * value_length is exactly 0) */
      do {
        const size_t len = size < buflen ? size : buflen;
        res = dicm_parser_read_bytes(parser, buf, len);
        if (res != 0) {
          goto error;
        }
        size -= len;
      } while (size != 0);
      fprintf(out, "%s", events[etype]);
      fprintf(out, " %.*s", (int)oldsize, buf);
      break;
    default:
      res = dicm_parser_get_key(parser, &key);
      assert(res < 0);
      res = dicm_parser_get_size(parser, &size);
      assert(res < 0);
      res = dicm_parser_read_bytes(parser, buf, buflen);
      assert(res < 0);
      fprintf(out, "%s", events[etype]);
    }

    /* Are we finished? */
    done = (etype == DICM_DOCUMENT_END_EVENT);
    fprintf(out, "\n");
    // not a benchmark tool:
    fflush(out);
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
