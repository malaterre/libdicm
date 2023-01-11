#include <dicm.h>
#include <yaml.h>

#include <assert.h> /* assert */

static void my_log(int log_level, const char *msg) {
  fprintf(stderr, "LOG: %d - %s\n", log_level, msg);
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
  yaml_emitter_t g_emitter;
  yaml_event_t event;

  dicm_configure_log_msg(my_log);

  if (argc < 2) {
    dicm_src_file_create(&src, stdin);
  } else if (argc > 2 || argv[1][0] == '-') {
    fprintf(stderr, "usage: dicm2yaml [filename]\n");
    exit(1);
  } else {
    stream = fopen(argv[1], "rb");
    dicm_src_file_create(&src, stream);
  }

  if (dicm_parser_create(&parser) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "parser\n");
    exit(1);
  }

  dicm_parser_set_input(parser, src);

  // YAML:
  yaml_emitter_initialize(&g_emitter);
  yaml_emitter_set_output_file(&g_emitter, stdout);

  /* Read the event sequence. */
  while (!done) {
    const int next = dicm_parser_next_event(parser);
    if (next < 0) {
      goto error;
    }
    /* Get the next event. */
    const enum dicm_event_type etype = next;
    switch (etype) {
    case DICM_KEY_EVENT:
      res = dicm_parser_get_key(parser, &key);
      assert(res == 0);
      snprintf(buf, buflen, "%08x", key.tag);
      yaml_scalar_event_initialize(&event, NULL, NULL, buf, strlen(buf), 1, 1,
                                   YAML_ANY_SCALAR_STYLE);
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

        yaml_scalar_event_initialize(&event, NULL, NULL, buf, len, 1, 1,
                                     YAML_ANY_SCALAR_STYLE);

        size -= len;
      } while (size != 0);
      break;

      /* */
    case DICM_STREAM_START_EVENT:
      yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
      break;
    case DICM_STREAM_END_EVENT:
      yaml_stream_end_event_initialize(&event);
      break;
    case DICM_DOCUMENT_START_EVENT:
      yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1);
      break;
    default:
      assert(0);
    }
    const int status = yaml_emitter_emit(&g_emitter, &event);
    assert(status != 0);

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
