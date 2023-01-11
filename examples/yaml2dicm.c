#include <dicm.h>
#include <yaml.h>

#include <assert.h>  /* assert */
#include <stdbool.h> /* bool */

static void my_log(int log_level, const char *msg) {
  fprintf(stderr, "LOG: %d - %s\n", log_level, msg);
}

enum my_state {
  STATE_INVALID = -1,
  STATE_KEY,
  STATE_VR1,
  STATE_VR2,
  STATE_VR3,
  STATE_VALUE1,
  STATE_VALUE2,
  STATE_VALUE3,
  STATE_VALUE4,
  STATE_VALUE5,
};

int main(int argc, char *argv[]) {
  struct dicm_emitter *emitter;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const size_t buflen = sizeof buf;
  struct dicm_dst *dst;
  FILE *stream = NULL;
  yaml_parser_t parser;
  yaml_event_t event;

  // YAML
  if (argc < 2)
    stream = stdin;
  else if (argc > 2 || argv[1][0] == '-') {
    fprintf(stderr, "usage: yaml-json [filename]\n");
    exit(1);
  } else if (!(stream = fopen(argv[1], "r"))) {
    perror("json-yaml");
    exit(1);
  }
  if (!yaml_parser_initialize(&parser)) {
    fprintf(stderr, "yaml-json: failed to initialize "
                    "parser\n");
    exit(1);
  }

  yaml_parser_set_input_file(&parser, stream);

  // DICOM:
  dicm_configure_log_msg(my_log);
  if (dicm_emitter_create(&emitter) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "emitter\n");
    exit(1);
  }

  dicm_dst_file_create(&dst, stdout);
  dicm_emitter_set_output(emitter, dst);

  enum my_state state = STATE_INVALID;
  enum dicm_event_type detype;
  /* Read the event sequence. */
  while (!done) {
    if (!yaml_parser_parse(&parser, &event)) {
      fprintf(stderr, "yaml-json: failed to parse "
                      "event\n");
      exit(1);
    }

    const yaml_event_type_t yetype = event.type;
    switch (yetype) {
    case YAML_STREAM_START_EVENT:
      detype = DICM_STREAM_START_EVENT;
      break;
    case YAML_DOCUMENT_START_EVENT:
      detype = DICM_DOCUMENT_START_EVENT;
      break;
    case YAML_DOCUMENT_END_EVENT:
      assert(0);
      // status = yajl_gen_status_ok;
      break;

    case YAML_STREAM_END_EVENT:
      assert(0);
      break;

    case YAML_SCALAR_EVENT:
      assert(state == STATE_KEY || state == STATE_VR2 || state == STATE_VR3 ||
             state == STATE_VALUE1 || state == STATE_VALUE3);
      if (state == STATE_KEY) {
        unsigned char *val;
        size_t len;

        val = event.data.scalar.value;
        len = event.data.scalar.length;
        assert(!event.data.scalar.quoted_implicit);
        assert(strlen(val) == len);
        assert(len == 8);
        uint32_t tag;
        fprintf(stderr, "%s\n", val);
        int res = sscanf(val, "%08x", &tag);
        assert(res == 1);
        key.tag = tag;
        state = STATE_VR1;
      } else if (state == STATE_VR2) {
        unsigned char *val;
        size_t len;

        val = event.data.scalar.value;
        len = event.data.scalar.length;
        assert(!event.data.scalar.quoted_implicit);
        assert(strlen(val) == len);
        assert(len == 2);
        assert(strcmp(val, "vr") == 0);
        state = STATE_VR3;
      } else if (state == STATE_VR3) {
        unsigned char *val;
        size_t len;

        val = event.data.scalar.value;
        len = event.data.scalar.length;
        assert(!event.data.scalar.quoted_implicit);
        assert(strlen(val) == len);
        assert(len == 2);
        uint32_t vr = 0;
        int res = sscanf(val, "%s", &vr);
        assert(res == 1);
        key.vr = vr;
        state = STATE_VALUE1;
        detype = DICM_KEY_EVENT;
        res = dicm_emitter_set_key(emitter, &key);
        assert(res == 0);
      } else if (state == STATE_VALUE1) {
        unsigned char *val;
        size_t len;

        val = event.data.scalar.value;
        len = event.data.scalar.length;
        assert(!event.data.scalar.quoted_implicit);
        assert(len == 5);
        assert(strcmp(val, "Value") == 0);
        state = STATE_VALUE2;
      } else if (state == STATE_VALUE3) {
        unsigned char *val;
        size_t len;

        val = event.data.scalar.value;
        len = event.data.scalar.length;
        assert(!event.data.scalar.quoted_implicit);

        uint32_t size;
        size = len;
        res = dicm_emitter_set_value_length(emitter, &size);
        assert(res == 0);
        assert(len < buflen);
        memcpy(buf, val, len);

        do {
          const size_t len = size < buflen ? size : buflen;
          res = dicm_emitter_write_value(emitter, buf, len);
          assert(res == 0);

          size -= len;
        } while (size != 0);

        detype = DICM_VALUE_EVENT;
        state = STATE_VALUE4;

      } else {
        assert(0);
      }

      if (state != STATE_VALUE1 && state != STATE_VALUE4) {
        yaml_event_delete(&event);
        continue;
      } else {
        break;
      }

    case YAML_SEQUENCE_START_EVENT:
      assert(state == STATE_VALUE2);
      state = STATE_VALUE3;
      yaml_event_delete(&event);
      continue;

    case YAML_SEQUENCE_END_EVENT:
      assert(state == STATE_VALUE4);
      state = STATE_VALUE5;
      yaml_event_delete(&event);
      continue;

    case YAML_MAPPING_START_EVENT:
      fprintf(stderr, "mapping event\n");
      assert(state == STATE_INVALID || state == STATE_VR1);
      if (state == STATE_INVALID) {
        state = STATE_KEY;
      } else {
        state = STATE_VR2;
      }
      yaml_event_delete(&event);
      continue;

    case YAML_MAPPING_END_EVENT:
      assert(state == STATE_VALUE5);
      state = STATE_KEY;
      yaml_event_delete(&event);
      continue;

    case YAML_ALIAS_EVENT:
      fprintf(stderr, "yaml-json: aliases are not "
                      "yet supported\n");
      exit(1);

    default:
      fprintf(stderr,
              "yaml-json: unexpected event "
              "type: %d\n",
              (int)event.type);
      exit(1);
    }

    //		if (status != yajl_gen_status_ok) {
    //			fprintf(stderr, "yaml-json: failed to emit "
    //			    "value\n");
    //			exit(1);
    //		}

    res = dicm_emitter_emit(emitter, detype);
    assert(res == 0);

    yaml_event_delete(&event);
    /* Are we finished? */
    done = (yetype == YAML_STREAM_END_EVENT);
  }

  /* Destroy the emitter object. */
  dicm_delete(emitter);
  if (stream)
    fclose(stream);
  dicm_delete(dst);
  return 0;

error:
  dicm_delete(emitter);
  if (stream)
    fclose(stream);
  dicm_delete(dst);
  return 1;
}
