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

static int64_t my_read(struct dicm_src *src, void *buf, size_t size);

int64_t my_read(struct dicm_src *const src, void *buf, size_t size) {
  struct dicm_src_user *self = (struct dicm_src_user *)src;
  FILE *stream = self->data;
  const size_t read = fread(buf, 1, size, stream);
  const int ret = (int)read;
  return ret;
}

int main(int argc, char *argv[]) {
  struct dicm_parser *parser;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const size_t buflen = sizeof buf;
  struct dicm_src *src;
  FILE *stream = NULL;

  dicm_configure_log_msg(my_log);

  if (argc < 2) {
    dicm_src_file_create(&src, stdin);
  } else if (argc > 2 || argv[1][0] == '-') {
    fprintf(stderr, "usage: dummy [filename]\n");
    exit(1);
  } else {
    stream = fopen(argv[1], "rb");
    dicm_src_user_create(&src, stream, my_read, NULL);
  }

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
    printf("EVENT: %s %d\n", events[etype], etype);

    /*
      ...
      Process the event.
      ...
    */
    switch (etype) {
    case DICM_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      break;
    case DICM_VALUE_EVENT:
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
  if (stream)
    fclose(stream);
  dicm_delete(src);
  return 0;

error:
  dicm_delete(parser);
  if (stream)
    fclose(stream);
  dicm_delete(src);
  return 1;
}
