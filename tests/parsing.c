#include "dicm.h"

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* EXIT_SUCCESS */

static const char *events[] = {
    "stream-start", "stream-end",     "dataset-start", "dataset-end",
    "element-key",  "fragment",       "element-value", "item-start",
    "item-end",     "sequence-start", "sequence-end",
};

int parsing(int argc, char *argv[]) {
  struct dicm_parser *parser;
  struct dicm_io *io;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  size_t size;
  int res;
  const size_t buflen = sizeof buf;

  dicm_input_file_create(&io, argv[1]);
  FILE *out = fopen(argv[2], "w");

  if (dicm_parser_create(&parser) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "parser\n");
    exit(1);
  }

  dicm_parser_set_input(parser, io);

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
    case DICM_ELEMENT_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      fprintf(out, " %08x", key.tag);
      break;
    case DICM_ELEMENT_VALUE_EVENT:
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
      fprintf(out, " %.*s", len, buf);
      break;
    }

    /* Are we finished? */
    done = (etype == DICM_STREAM_END_EVENT);
    fprintf(out, "\n");
  }

  /* Destroy the Parser object. */
  dicm_delete(parser);
  dicm_delete(io);
  fclose(out);
  return EXIT_SUCCESS;

error:
  dicm_delete(parser);
  dicm_delete(io);
  fclose(out);
  return EXIT_FAILURE;
}
