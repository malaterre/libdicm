#include <dicm.h>

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* exit() */

static const char *events[] = {
    "DICM_STREAM_START_EVENT",  "DICM_STREAM_END_EVENT",
    "DICM_DATASET_START_EVENT", "DICM_DATASET_END_EVENT",
    "DICM_ELEMENT_KEY_EVENT",   "DICM_FRAGMENT_EVENT",
    "DICM_ELEMENT_VALUE_EVENT", "DICM_ITEM_START_EVENT",
    "DICM_ITEM_END_EVENT",      "DICM_SEQUENCE_START_EVENT",
    "DICM_SEQUENCE_END_EVENT",
};

static void my_log(int log_level, const char *msg) {
  fprintf(stderr, "LOG: %d - %s\n", log_level, msg);
}

FILE *stream;

static int my_read(void *const io, void *buf, size_t size) {
  const size_t read = fread(buf, 1, size, stream);
  const int ret = (int)read;
  return ret;
}

int main(int argc, char *argv[]) {
  struct dicm_parser *parser;
  struct dicm_io *io;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  size_t size;
  int res;
  const size_t buflen = sizeof buf;

  dicm_configure_log_msg(my_log);

  if (argc < 2)
    dicm_input_stream_create(&io);
  else if (argc > 2 || argv[1][0] == '-') {
    fprintf(stderr, "usage: dummy [filename]\n");
    exit(1);
  } else {
    dicm_input_file_create(&io, argv[1]);
    // stream = fopen(argv[1], "rb");
    // dicm_create(&io, my_read, NULL, NULL);
  }

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
    printf("EVENT: %s %d\n", events[etype], etype);

    /*
      ...
      Process the event.
      ...
    */
    switch (etype) {
    case DICM_ELEMENT_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      break;
    case DICM_ELEMENT_VALUE_EVENT:
      res = dicm_parser_get_value_length(parser, &size);
      assert(res == 0);
      /* do/while loop trigger at least one event (even in the case where
       * value_length is exactly 0) */
      do {
        const size_t len = size < buflen ? size : buflen;
        res = dicm_parser_read_value(parser, buf, len);
        assert(res == 0);
        size -= len;
      } while (size != 0);
      break;
    }

    /* Are we finished? */
    done = (etype == DICM_STREAM_END_EVENT);
  }

  /* Destroy the Parser object. */
  dicm_delete(parser);
  dicm_delete(io);
  // fclose(stream);
  return 0;

error:
  dicm_delete(parser);
  dicm_delete(io);
  return 1;
}
