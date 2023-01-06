#include "dicm.h"

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strlen */

static const char *events[] = {
    "stream-start", "stream-end",     "document-start", "document-end",
    "key",          "fragment",       "value",          "item-start",
    "item-end",     "sequence-start", "sequence-end"};

static int get_type(const char *line) {
  const unsigned int n = sizeof(events) / sizeof(*events);
  for (unsigned int i = 0; i < n; ++i) {
    if (strncmp(line, events[i], strlen(events[i])) == 0) {
      return i;
    }
  }
  assert(0);
  return -1;
}

static void my_log(int log_level, const char *msg) {
  fprintf(stderr, "LOG: %d - %s\n", log_level, msg);
}

int emitting(int argc, char *argv[]) {
  struct dicm_emitter *emitter;
  struct dicm_dst *dst;
  struct dicm_key key;
  int done = 0;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const size_t buflen = sizeof buf;
  const char *transfer_syntax = argv[1];
  const char *infilename = argv[2];
  const char *outfilename = argv[3];
  FILE *in = fopen(infilename, "r");
  FILE *out = fopen(outfilename, "wb");

  dicm_configure_log_msg(my_log);

  dicm_dst_file_create(&dst, out);

  if (dicm_emitter_create(&emitter) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "emitter\n");
    exit(1);
  }

  dicm_emitter_set_output(emitter, dst);

  char *line;
  uint32_t tag;
  char vr_str[2 + 1];
  while (line = fgets(buf, sizeof buf, in)) {
    fprintf(stdout, "%s\n", line);
    enum dicm_event_type etype = get_type(line);
    switch (etype) {
    case DICM_KEY_EVENT:
      res = sscanf(line, "%*s %08x %s", &tag, vr_str);
      assert(res == 2);
      key.tag = tag;
      key.vr = 0;
      memcpy(&key.vr, vr_str, 2);
      res = dicm_emitter_set_key(emitter, &key);
      assert(res == 0);
      break;
    case DICM_VALUE_EVENT:
      res = sscanf(line, "%*s %[^\n]s", buf);
      assert(res == 1);
      size = strlen(buf);
      res = dicm_emitter_set_value_length(emitter, &size);
      assert(res == 0);
      res = dicm_emitter_write_value(emitter, buf, size);
      assert(res == 0);
      break;
    }
    res = dicm_emitter_emit(emitter, etype);
    assert(res == 0);
  }

  dicm_delete(emitter);
  dicm_delete(dst);
  fclose(in);
  fclose(out);
  return EXIT_SUCCESS;
}
