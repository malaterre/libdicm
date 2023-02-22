#include "dicm_emitter.h"

#include "dicm_dst.h"
#include "dicm_item.h"
#include "dicm_private.h"

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc */

// FIXME I need to define a name without spaces:
typedef struct level_emitter level_emitter_t;
struct emitter {
  struct dicm_emitter emitter;

  /* data */
  struct dicm_dst *dst;

  /* the current item state */
  enum state current_item_state;

  /* current pos in value_length */
  uint32_t value_length_pos;

  /* level emitters */
  array(level_emitter_t) * level_emitters;
};

static DICM_CHECK_RETURN int emitter_destroy(struct object *) DICM_NONNULL();

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

static inline struct level_emitter *
emitter_get_level_emitter(struct emitter *emitter) {
  return &array_back(emitter->level_emitters);
}

static inline enum state emitter_get_state(struct emitter *emitter) {
  return emitter->current_item_state;
}

static inline bool emitter_is_root_dataset(const struct emitter *emitter) {
  return emitter->level_emitters->size == 1;
}

#if 0
void init_root_level_emitter(struct _level_emitter *new_item,
                           enum dicm_structure_type);
#else
void encap_init_level_emitter(struct level_emitter *new_item);
void ivrle_init_level_emitter(struct level_emitter *new_item);
void evrle_init_level_emitter(struct level_emitter *const new_item);
void evrbe_init_level_emitter(struct level_emitter *new_item);

void init_level_emitter_item(struct level_emitter *new_item);
#endif

static inline void
emitter_set_root_level(struct emitter *emitter,
                       const enum dicm_structure_type structure_type,
                       const enum state current_state) {
  assert(current_state == STATE_INVALID);
  emitter->current_item_state = current_state;

  emitter->value_length_pos = VL_UNDEFINED;
  const struct level_emitter new_item = {.da = 0};
  array_push(emitter->level_emitters, new_item);
#if 0
  init_root_level_emitter(&array_back(emitter->level_emitters), structure_type);
#else
  struct level_emitter *root_item = &array_back(emitter->level_emitters);
  switch (structure_type) {
  case DICM_STRUCTURE_ENCAPSULATED:
    encap_init_level_emitter(root_item);
    break;
  case DICM_STRUCTURE_IMPLICIT:
    ivrle_init_level_emitter(root_item);
    break;
  case DICM_STRUCTURE_EXPLICIT_LE:
    evrle_init_level_emitter(root_item);
    break;
  case DICM_STRUCTURE_EXPLICIT_BE:
    evrbe_init_level_emitter(root_item);
    break;
  default:
    assert(0);
  }
#endif
  assert(emitter_is_root_dataset(emitter));
}

#define level_emitter_next_event(t, state, dst, event)                         \
  ((t)->vtable->level_emitter.fp_next_event((t), (state), (dst), (event)))
#define level_emitter_vl_token(t, dst, tok)                                    \
  ((t)->vtable->level_emitter.fp_vl_token((t), (dst), (tok)))
#define level_emitter_next_level(t, state)                                     \
  ((t)->vtable->level_emitter.fp_next_level((t), (state)))

static inline void emitter_push_level(struct emitter *emitter,
                                      const enum state current_state) {
  struct level_emitter *level_emitter = emitter_get_level_emitter(emitter);
  struct level_emitter new_item =
      level_emitter_next_level(level_emitter, current_state);
  array_push(emitter->level_emitters, new_item);
}

static inline void emitter_pop_level(struct emitter *emitter,
                                     const enum state current_state) {
  struct level_emitter *level_emitter_old = emitter_get_level_emitter(emitter);
  (void)array_pop(emitter->level_emitters);
  struct level_emitter *level_emitter = emitter_get_level_emitter(emitter);
  //  assert(level_emitter->da.vl == level_emitter->sequence_length);
  assert(level_emitter_old->sequence_length_pos ==
             level_emitter_old->sequence_length2 ||
         level_emitter_old->sequence_length2 == VL_UNDEFINED);
  level_emitter->item_length_pos += level_emitter_old->sequence_length_pos;
  assert(level_emitter->item_length_pos <= level_emitter->item_length2);
}

static enum state emitter_emit(struct emitter *emitter,
                               const enum dicm_event_type next) {
  ASSUME(emitter->current_item_state != STATE_INVALID);
  ASSUME(next >= 0);
  // special init case
  if (next == DICM_DOCUMENT_START_EVENT) {
    assert(emitter_get_state(emitter) == STATE_INIT);
    assert(emitter->dst);
    emitter->current_item_state = STATE_STARTDOCUMENT;
    return STATE_STARTDOCUMENT;
  }

  // user has called set_key / set_size, now compute directly new state using
  // current level_emitter:

  // else compute new state from event:
  struct level_emitter *level_emitter = emitter_get_level_emitter(emitter);
  const enum state new_state = level_emitter_next_event(
      level_emitter, emitter->current_item_state, emitter->dst, next);

  // FIXME: should not expose detail frag vs item here:
  switch (new_state) {
  case STATE_STARTSEQUENCE:
    assert(next == DICM_SEQUENCE_START_EVENT);
    emitter_push_level(emitter, new_state);
    break;
  case STATE_STARTFRAGMENTS:
    assert(next == DICM_SEQUENCE_START_EVENT);
    emitter_push_level(emitter, new_state);
    break;
  case STATE_ENDSEQUENCE:
    assert(next == DICM_SEQUENCE_END_EVENT);
    emitter_pop_level(emitter, new_state);
    break;
  default:;
  }

  // update to new state (even if invalid):
  emitter->current_item_state = new_state;
  return new_state;
}

int emitter_destroy(struct object *const self) {
  struct emitter *emitter = (struct emitter *)self;
  array_free(emitter->level_emitters);
  free(emitter);
  return 0;
}

/* public API */
int dicm_emitter_set_output(struct dicm_emitter *self, const int structure_type,
                            struct dicm_dst *dst) {
  struct emitter *emitter = (struct emitter *)self;
  // clear any previous run:
  emitter->level_emitters->size = 0;
  emitter->current_item_state = STATE_INVALID;
  const enum dicm_structure_type estype = structure_type;
  // update ready state:
  emitter->dst = dst;
  enum state new_state = STATE_INVALID;
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
  struct emitter *emitter = (struct emitter *)self;
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
  struct emitter *emitter = (struct emitter *)self_;
  const enum state current_state = emitter_get_state(emitter);
  switch (current_state) {
  case STATE_STARTDOCUMENT:
  case STATE_VALUE:
  case STATE_STARTITEM:
  case STATE_ENDSEQUENCE: {
    struct level_emitter *level_emitter = emitter_get_level_emitter(emitter);
    const enum dicm_event_type next = DICM_KEY_EVENT;
    const enum token token = event2token(next);
    assert(token == TOKEN_KEY);
    struct key_info *da = &level_emitter->da;
    da->tag = key->tag;
    da->vr = key->vr;
    /* FIXME: validate key */
    return 0;
  } break;
  default:;
  }

  return -1;
}

int dicm_emitter_set_size(struct dicm_emitter *self_, const uint32_t len) {
  struct emitter *emitter = (struct emitter *)self_;
  const enum state current_state = emitter_get_state(emitter);
  struct level_emitter *level_emitter = emitter_get_level_emitter(emitter);

  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_KEY:
    if (len == VL_UNDEFINED || len % 2 == 0) {
      struct key_info *da = &level_emitter->da;
      da->vl = len;
      emitter->value_length_pos = VL_UNDEFINED; // FIXME
      new_state = current_state;
    }
    break;
  case STATE_STARTSEQUENCE:
  case STATE_ENDITEM:
    if (len == VL_UNDEFINED || len % 2 == 0) {
      level_emitter->item_length2 = len;
      init_level_emitter_item(level_emitter);
      new_state = current_state;
    }
    break;
  case STATE_FRAGMENT:
    if (len == VL_UNDEFINED || len % 2 == 0) {
      struct key_info *da = &level_emitter->da;
      da->vl = len;
      emitter->value_length_pos = VL_UNDEFINED; // FIXME
      new_state = current_state;
    }
    break;
  default:
    assert(0);
  }

#if 0
  assert(current_state == STATE_STARTSEQUENCE ||
         current_state == STATE_ENDITEM || current_state == STATE_KEY ||
         current_state == STATE_FRAGMENT);

  if (len == VL_UNDEFINED || len % 2 == 0) {
    da->vl = len;
    emitter->value_length_pos = VL_UNDEFINED;

    return 0;
  }
  emitter->current_item_state = STATE_INVALID;
  return -1;
#endif
  return new_state == STATE_INVALID ? -1 : 0;
}

int dicm_emitter_write_bytes(struct dicm_emitter *self_, const void *ptr,
                             size_t len) {
  /* FIXME: make alignemnt ptr to uint16 */
  struct emitter *emitter = (struct emitter *)self_;
  const enum state current_state = emitter_get_state(emitter);
  struct level_emitter *level_emitter = emitter_get_level_emitter(emitter);
  assert(current_state == STATE_KEY || current_state == STATE_FRAGMENT);

  const uint32_t value_length = level_emitter->da.vl;
  assert(len <= value_length);
  const uint32_t to_write = (uint32_t)len;
  struct dicm_dst *dst = emitter->dst;
  //  const enum token tok = TOKEN_VALUE;
  /* Write VL */
  if (emitter->value_length_pos == VL_UNDEFINED) {
    const enum token tok =
        level_emitter_vl_token(level_emitter, dst, DICM_VALUE_EVENT);
    assert(tok == TOKEN_VALUE);
    // FIXME: to_write vs value_length
    assert(value_length != VL_UNDEFINED);
    level_emitter->item_length_pos += value_length;
    emitter->value_length_pos = 0;
  }

  /* Write actual value */
  int64_t dlen = dicm_dst_write(dst, ptr, to_write);
  assert(dlen == to_write);
  emitter->value_length_pos += to_write;
  assert(emitter->value_length_pos <= level_emitter->da.vl);

  return 0;
}

int dicm_emitter_create(struct dicm_emitter **pself) {
  struct emitter *self = (struct emitter *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->emitter;
    self->emitter.vtable = &g_vtable;
    array_new(level_emitter_t, self->level_emitters);

    return 0;
  }
  return -1;
}
