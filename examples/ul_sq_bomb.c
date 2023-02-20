#include <dicm.h>

#include <assert.h> /* assert() */
#include <stdio.h>  /* FILE* */
#include <stdlib.h> /* exit() */

/* undefined length SQ bomb */
/* https://en.wikipedia.org/wiki/Billion_laughs_attack */
static uint32_t num_nesting = 6400;

static void emit_level(struct dicm_emitter *emitter) {
  int res;
  struct dicm_key key;
  enum dicm_event_type etype;
  key.tag = 0x00082112;
  res = dicm_emitter_set_key(emitter, &key);
  assert(res >= 0);
  etype = DICM_KEY_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);
  etype = DICM_SEQUENCE_START_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);
  etype = DICM_ITEM_START_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);
  if (--num_nesting > 0) {
    emit_level(emitter);
  }
  etype = DICM_ITEM_END_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);
  etype = DICM_SEQUENCE_END_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);
}

int main(int argc, char *argv[]) {
  struct dicm_emitter *emitter;
  int done = 0;
  /* value */
  char buf[4096];
  uint32_t size;
  int res;
  const size_t buflen = sizeof buf;
  struct dicm_dst *dst;
  FILE *outstream = NULL;

  dicm_dst_file_create(&dst, stdout);

  if (dicm_emitter_create(&emitter) < 0) {
    fprintf(stderr, "dummy: failed to initialize "
                    "emitter\n");
    exit(1);
  }

  enum dicm_event_type etype;
  dicm_emitter_set_output(emitter, DICM_STRUCTURE_IMPLICIT, dst);

  /* Read the event sequence. */
  etype = DICM_DOCUMENT_START_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);

  emit_level(emitter);

  etype = DICM_DOCUMENT_END_EVENT;
  res = dicm_emitter_emit(emitter, etype);
  assert(res >= 0);

  dicm_delete(emitter);
  dicm_delete(dst);
  return 0;
}
