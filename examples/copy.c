#include <dicm.h>

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* exit() */

static void my_log(int log_level, const char *msg) {
  fprintf(stderr, "LOG: %d - %s\n", log_level, msg);
}

static int64_t my_read(struct dicm_src *src, void *buf, size_t size);

int64_t my_read(struct dicm_src *const src, void *buf, size_t size) {
  struct dicm_src_user *self = (struct dicm_src_user *)src;
  FILE *stream = self->data;
  const size_t read = fread(buf, 1, size, stream);
  const int64_t ret = (int64_t)read;
  return ret;
}

static int64_t my_write(struct dicm_dst *dst, const void *buf, size_t size);

int64_t my_write(struct dicm_dst *const dst, const void *buf, size_t size) {
  struct dicm_dst_user *self = (struct dicm_dst_user *)dst;
  FILE *stream = self->data;
  const size_t write = fwrite(buf, 1, size, stream);
  const int64_t ret = (int64_t)write;
  return ret;
}

int main(int argc, char *argv[]) {
  struct dicm_parser *parser;
  struct dicm_emitter *emitter;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const size_t buflen = sizeof buf;
  struct dicm_src *src;
  struct dicm_dst *dst;
  FILE *instream = NULL;
  FILE *outstream = NULL;

  dicm_configure_log_msg(my_log);

  if (argc < 2) {
    dicm_src_file_create(&src, stdin);
    dicm_dst_file_create(&dst, stdout);
  } else if (argc > 3 || argv[1][0] == '-') {
    fprintf(stderr, "usage: dummy [infile] [outfile]\n");
    exit(1);
  } else {
    instream = fopen(argv[1], "rb");
    outstream = fopen(argv[2], "wb");
    dicm_src_user_create(&src, instream, my_read, NULL);
    dicm_dst_user_create(&dst, outstream, my_write, NULL);
  }

  if (dicm_parser_create(&parser) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "parser\n");
    exit(1);
  }
  if (dicm_emitter_create(&emitter) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "emitter\n");
    exit(1);
  }

  dicm_parser_set_input(parser, DICM_STRUCTURE_ENCAPSULATED, src);
  dicm_emitter_set_output(emitter, DICM_STRUCTURE_ENCAPSULATED, dst);

  /* Read the event sequence. */
  while (!done) {
    const int next = dicm_parser_next_event(parser);
    if (next < 0) {
      goto error;
    }
    /* Get the next event. */
    enum dicm_event_type etype = next;

    /* Process the events */
    switch (etype) {
    case DICM_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      res = dicm_emitter_set_key(emitter, &key);
      assert(res == 0);
      break;
    case DICM_VALUE_EVENT:
      res = dicm_parser_get_value_length(parser, &size);
      assert(res == 0);
      res = dicm_emitter_set_value_length(emitter, &size);
      assert(res == 0);
      /* do/while loop trigger at least one event (even in the case where
       * value_length is exactly 0) */
      do {
        const size_t len = size < buflen ? size : buflen;
        res = dicm_parser_read_value(parser, buf, len);
        assert(res == 0);
        res = dicm_emitter_write_value(emitter, buf, len);
        assert(res == 0);

        size -= len;
      } while (size != 0);
      break;
    }

    res = dicm_emitter_emit(emitter, etype);
    assert(res == 0);

    /* Are we finished? */
    done = (etype == DICM_STREAM_END_EVENT);
  }

  /* Destroy the Parser object. */
  dicm_delete(parser);
  dicm_delete(emitter);
  if (instream)
    fclose(instream);
  if (outstream)
    fclose(outstream);
  dicm_delete(src);
  dicm_delete(dst);
  return 0;

error:
  dicm_delete(parser);
  dicm_delete(emitter);
  if (instream)
    fclose(instream);
  if (outstream)
    fclose(outstream);
  dicm_delete(src);
  dicm_delete(dst);
  return 1;
}
