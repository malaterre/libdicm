#include "dicm.h"

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* EXIT_SUCCESS */
#include <string.h> /* strlen */

static const char *events[] = {
    "document-start", "document-end",   "key",
    "fragment",       "value",          "item-start",
    "item-end",       "sequence-start", "sequence-end"};

static int get_type(const char *line) {
  const int n = sizeof(events) / sizeof(*events);
  for (int i = 0; i < n; ++i) {
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
  if (argc < 4)
    return EXIT_FAILURE;
  struct dicm_emitter *emitter;
  struct dicm_dst *dst;
  struct dicm_key key;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const char *structure = argv[1];
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

  if (structure == NULL || strcmp("evrle_encapsulated", structure) == 0) {
    dicm_emitter_set_output(emitter, DICM_STRUCTURE_ENCAPSULATED, dst);
  } else if (strcmp("ivrle_raw", structure) == 0) {
    dicm_emitter_set_output(emitter, DICM_STRUCTURE_IMPLICIT, dst);
  } else if (strcmp("evrle_raw", structure) == 0) {
    dicm_emitter_set_output(emitter, DICM_STRUCTURE_EXPLICIT_LE, dst);
  } else if (strcmp("evrbe_raw", structure) == 0) {
    dicm_emitter_set_output(emitter, DICM_STRUCTURE_EXPLICIT_BE, dst);
  } else {
    fprintf(stderr, "Invalid structure: %s\n", structure);
    exit(1);
  }

  char *line;
  uint32_t tag;
  char vr_str[2 + 1];
  int ret;
  while ((line = fgets(buf, sizeof buf, in))) {
    ret = EXIT_FAILURE;
    fprintf(stdout, "%s\n", line);
    const enum dicm_event_type etype = get_type(line);
    switch (etype) {
    case DICM_KEY_EVENT:
      res = sscanf(line, "%*s %08x %s", &tag, vr_str);
      assert(res == 2 || res == 1);
      key.tag = tag;
      key.vr = 0;
      if (res >= 2)
        memcpy(&key.vr, vr_str, 2);
      res = dicm_emitter_set_key(emitter, &key);
      assert(res == 0);
      break;
    case DICM_VALUE_EVENT:
      res = sscanf(line, "%*s %[^\n]s", buf);
      assert(res == 1 || res == -1);
      if (res == 1) {
        size = (uint32_t)strlen(buf);
      } else {
        size = 0;
      }
      res = dicm_emitter_set_size(emitter, size);
      assert(res == 0);
      res = dicm_emitter_write_bytes(emitter, buf, size);
      assert(res == 0);
      break;
    case DICM_ITEM_START_EVENT:
    case DICM_SEQUENCE_START_EVENT:
      res = sscanf(line, "%*s %08x", &size);
      assert(res == 1);
      res = dicm_emitter_set_size(emitter, size);
      assert(res == 0);
      break;
    case DICM_DOCUMENT_END_EVENT:
      assert(ret == EXIT_FAILURE);
      ret = EXIT_SUCCESS;
      break;
    default:;
    }
    res = dicm_emitter_emit(emitter, etype);
    assert(res >= 0);
  }

  dicm_delete(emitter);
  dicm_delete(dst);
  fclose(in);
  fclose(out);
  return ret;
}
