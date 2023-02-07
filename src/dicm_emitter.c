#include "dicm_emitter.h"

#include "dicm_dst.h"
#include "dicm_item.h"
#include "dicm_private.h"

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc */

// FIXME I need to define a name without spaces:
typedef struct _item_writer item_writer_t;
struct _emitter {
  struct dicm_emitter emitter;

  /* data */
  struct dicm_dst *dst;

  /* the current item state */
  enum dicm_state current_item_state;

  /* current pos in value_length */
  uint32_t value_length_pos;

  /* item writers */
  array(item_writer_t) * item_writers;
};

static DICM_CHECK_RETURN int emitter_destroy(struct object *) DICM_NONNULL;

static struct emitter_vtable const g_vtable = {
    /* object interface */
    .object = {.fp_destroy = emitter_destroy},
/* emitter interface */
#if 0
    .emitter = {.fp_write_key = NULL,
                .fp_write_value_length = NULL,
                .fp_write_value = NULL}
#endif
};

static inline struct _item_writer *
emitter_get_level_writer(struct _emitter *emitter) {
  return &array_back(emitter->item_writers);
}

static inline enum dicm_state emitter_get_state(struct _emitter *emitter) {
  return emitter->current_item_state;
}

static inline bool emitter_is_root_dataset(const struct _emitter *emitter) {
  return emitter->item_writers->size == 1;
}

#if 0
void init_root_item_writer(struct _item_writer *new_item,
                           enum dicm_structure_type);
#else
void encap_init_item_writer(struct _item_writer *new_item);
void ivrle_init_item_writer(struct _item_writer *new_item);
void evrle_init_item_writer(struct _item_writer *new_item);
void evrbe_init_item_writer(struct _item_writer *new_item);
#endif

static inline void
emitter_set_root_level(struct _emitter *emitter,
                       const enum dicm_structure_type structure_type,
                       const enum dicm_state current_state) {
  assert(current_state == STATE_INVALID);
  emitter->current_item_state = current_state;

  emitter->value_length_pos = VL_UNDEFINED;
  struct _item_writer new_item = {};
  array_push(emitter->item_writers, new_item);
#if 0
  init_root_item_writer(&array_back(emitter->item_writers), structure_type);
#else
  struct _item_writer *root_item = &array_back(emitter->item_writers);
  switch (structure_type) {
  case DICM_STRUCTURE_ENCAPSULATED:
    encap_init_item_writer(root_item);
    break;
  case DICM_STRUCTURE_IMPLICIT:
    ivrle_init_item_writer(root_item);
    break;
  case DICM_STRUCTURE_EXPLICIT_LE:
    evrle_init_item_writer(root_item);
    break;
  case DICM_STRUCTURE_EXPLICIT_BE:
    evrbe_init_item_writer(root_item);
    break;
  default:
    assert(0);
  }
#endif
  assert(emitter_is_root_dataset(emitter));
}

#define item_writer_next_event(t, state, dst, event)                           \
  ((t)->vtable->writer.fp_next_event((t), (state), (dst), (event)))
#define item_writer_vl_token(t, dst, tok)                                      \
  ((t)->vtable->writer.fp_vl_token((t), (dst), (tok)))
#define item_writer_next_level(t, state)                                       \
  ((t)->vtable->writer.fp_next_level((t), (state)))

static inline void emitter_push_level(struct _emitter *emitter,
                                      const enum dicm_state current_state) {
  struct _item_writer *item_writer = emitter_get_level_writer(emitter);
  struct _item_writer new_item =
      item_writer_next_level(item_writer, current_state);
  array_push(emitter->item_writers, new_item);
}

static inline void emitter_pop_level(struct _emitter *emitter,
                                     const enum dicm_state current_state) {
  (void)current_state;
  (void)array_pop(emitter->item_writers);
}

static enum dicm_state emitter_emit(struct _emitter *emitter,
                                    const enum dicm_event_type next) {
  assert(emitter->current_item_state != STATE_INVALID);
  assert(next >= 0);
  // special init case
  if (emitter_get_state(emitter) == STATE_INIT) {
    assert(emitter->dst);
    assert(next == DICM_DOCUMENT_START_EVENT);
    emitter->current_item_state = STATE_STARTDOCUMENT;
    return STATE_STARTDOCUMENT;
  }

  // else compute new state from event:
  struct _item_writer *item_writer = emitter_get_level_writer(emitter);
  const enum dicm_state new_state = item_writer_next_event(
      item_writer, emitter->current_item_state, emitter->dst, next);

  // FIXME: should not expose detail frag vs item here:
  switch (new_state) {
  case STATE_STARTSEQUENCE:
  case STATE_STARTFRAGMENTS:
    emitter_push_level(emitter, new_state);
    break;
  case STATE_ENDSEQUENCE:
    emitter_pop_level(emitter, new_state);
    break;
  default:;
  }

  // update to new state (even if invalid):
  emitter->current_item_state = new_state;
  return new_state;
}

int emitter_destroy(struct object *const self) {
  struct _emitter *emitter = (struct _emitter *)self;
  array_free(emitter->item_writers);
  free(emitter);
  return 0;
}

/* public API */
int dicm_emitter_set_output(struct dicm_emitter *self, const int structure_type,
                            struct dicm_dst *dst) {
  struct _emitter *emitter = (struct _emitter *)self;
  // clear any previous run:
  emitter->item_writers->size = 0;
  emitter->current_item_state = STATE_INVALID;
  const enum dicm_structure_type estype = structure_type;
  // update ready state:
  emitter->dst = dst;
  enum dicm_state new_state = STATE_INVALID;
  switch (estype) {
  case DICM_STRUCTURE_ENCAPSULATED:
  case DICM_STRUCTURE_IMPLICIT:
  case DICM_STRUCTURE_EXPLICIT_LE:
  case DICM_STRUCTURE_EXPLICIT_BE:
    emitter_set_root_level(emitter, estype, STATE_INVALID);
    new_state = STATE_INIT;
    break;
  default:;
  }
  emitter->current_item_state = new_state;
  return new_state;
}

int dicm_emitter_emit(struct dicm_emitter *self, const int event_type) {
  struct _emitter *emitter = (struct _emitter *)self;
  if (emitter->current_item_state == STATE_INVALID) {
    return STATE_INVALID;
  }
  if (event_type < 0) {
    emitter->current_item_state = STATE_INVALID;
    return STATE_INVALID;
  }
  // else valid event type / valid state:
  const enum dicm_event_type next = event_type;
  return emitter_emit(emitter, next);
}

int dicm_emitter_set_key(struct dicm_emitter *self_,
                         const struct dicm_key *key) {
  struct _emitter *emitter = (struct _emitter *)self_;
  const enum dicm_state current_state = emitter_get_state(emitter);
  assert(current_state == STATE_STARTDOCUMENT || current_state == STATE_VALUE ||
         current_state == STATE_STARTITEM ||
         current_state == STATE_ENDSEQUENCE);

  struct _item_writer *item_writer = emitter_get_level_writer(emitter);
  const enum dicm_event_type next = DICM_KEY_EVENT;
  const enum dicm_token token = event2token(next);
  assert(token == TOKEN_KEY);
  struct _attribute *da = &item_writer->da;
  da->tag = key->tag;
  da->vr = key->vr;
  /* FIXME: validate key */

  return 0;
}

int dicm_emitter_set_value_length(struct dicm_emitter *self_,
                                  const uint32_t len) {
  struct _emitter *emitter = (struct _emitter *)self_;
  const enum dicm_state current_state = emitter_get_state(emitter);
  assert(current_state == STATE_KEY || current_state == STATE_FRAGMENT);

  struct _item_writer *item_writer = emitter_get_level_writer(emitter);
  struct _attribute *da = &item_writer->da;
  if (len % 2 == 0) {
    da->vl = len;
    emitter->value_length_pos = VL_UNDEFINED;

    return 0;
  }
  emitter->current_item_state = STATE_INVALID;
  return -1;
}

int dicm_emitter_write_value(struct dicm_emitter *self_, const void *ptr,
                             size_t len) {
  /* FIXME: make alignemnt ptr to uint16 */
  struct _emitter *emitter = (struct _emitter *)self_;
  const enum dicm_state current_state = emitter_get_state(emitter);
  struct _item_writer *item_writer = emitter_get_level_writer(emitter);
  assert(current_state == STATE_KEY || current_state == STATE_FRAGMENT);

  const uint32_t value_length = item_writer->da.vl;
  assert(len <= value_length);
  const uint32_t to_write = (uint32_t)len;
  struct dicm_dst *dst = emitter->dst;
  const enum dicm_token tok = TOKEN_VALUE;
  /* Write VL */
  if (emitter->value_length_pos == VL_UNDEFINED) {
    const enum dicm_state new_state =
        item_writer_vl_token(item_writer, dst, tok);
    assert(new_state == STATE_VALUE);
    emitter->value_length_pos = 0;
  }

  /* Write actual value */
  int64_t err = dicm_dst_write(dst, ptr, to_write);
  assert(err == to_write);
  emitter->value_length_pos += to_write;
  assert(emitter->value_length_pos <= item_writer->da.vl);

  return 0;
}

int dicm_emitter_create(struct dicm_emitter **pself) {
  struct _emitter *self = (struct _emitter *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->emitter;
    self->emitter.vtable = &g_vtable;
    array_new(item_writer_t, self->item_writers);

    return 0;
  }
  return -1;
}
