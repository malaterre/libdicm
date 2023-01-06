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

  /* item writers */
  array(item_writer_t) * item_writers;
};

static DICM_CHECK_RETURN int _emitter_destroy(struct object *) DICM_NONNULL;

static struct emitter_vtable const g_vtable = {
    /* object interface */
    .object = {.fp_destroy = _emitter_destroy},
    /* emitter interface */
    .emitter = {.fp_write_key = NULL,
                .fp_write_value_length = NULL,
                .fp_write_value = NULL}};

static inline enum dicm_state get_current_state(struct _emitter *emitter) {
  return array_back(emitter->item_writers).current_item_state;
}

int dicm_emitter_set_output(struct dicm_emitter *self, struct dicm_dst *dst) {
  struct _emitter *emitter = (struct _emitter *)self;
  emitter->dst = dst;
  // update ready state:
  array_back(emitter->item_writers).current_item_state = STATE_INIT;
  return 0;
}

static inline struct _item_writer *get_item_writer(struct _emitter *emitter) {
  return &array_back(emitter->item_writers);
}

int _ds_writer_next_token(struct _item_writer *self, struct dicm_dst *dst,
                          const enum dicm_event_type next);
int _item_writer_next_token(struct _item_writer *self, struct dicm_dst *dst,
                            const enum dicm_event_type next);
int _fragments_writer_next_token(struct _item_writer *self,
                                 struct dicm_dst *dst,
                                 const enum dicm_event_type next);

static inline void push_item_writer(struct _emitter *emitter,
                                    const enum dicm_state current_state) {
  assert(current_state == STATE_STARTSEQUENCE);
  assert(array_back(emitter->item_writers).current_item_state == current_state);

  struct _item_writer new_item = {.current_item_state = current_state,
                                  .fp_next_token = _item_writer_next_token};
  array_push(emitter->item_writers, new_item);
}

static inline void push_fragments_writer(struct _emitter *emitter,
                                         const enum dicm_state current_state) {
  assert(current_state == STATE_STARTFRAGMENTS);
  assert(array_back(emitter->item_writers).current_item_state == current_state);

  struct _item_writer new_item = {.current_item_state = current_state,
                                  .fp_next_token =
                                      _fragments_writer_next_token};
  array_push(emitter->item_writers, new_item);
}

static inline void pop_item_writer(struct _emitter *emitter,
                                   const enum dicm_state current_state) {
  assert(array_back(emitter->item_writers).current_item_state == current_state);

  (void)array_pop(emitter->item_writers);
  struct _item_writer *item_writer = &array_back(emitter->item_writers);
  assert(item_writer->current_item_state == STATE_STARTSEQUENCE ||
         item_writer->current_item_state == STATE_STARTFRAGMENTS);
  item_writer->current_item_state = current_state; // re-initialize
}

static int dicm_emitter_emit_next(struct _emitter *emitter,
                                  const enum dicm_event_type next) {
  struct _item_writer *item_writer = get_item_writer(emitter);
  // special init case
  if (get_current_state(emitter) == STATE_INIT) {
    assert(emitter->dst);
    assert(next == DICM_STREAM_START_EVENT);
    item_writer->current_item_state = STATE_STARTSTREAM;
    return next == DICM_STREAM_START_EVENT ? 0 : -1;
  }

  // else get next dicm token:
  const int ret = item_writer->fp_next_token(item_writer, emitter->dst, next);
  assert(ret >= 0);
  // at this point item_writer->current_item_state has been updated

  switch (get_current_state(emitter)) {
  case STATE_STARTSEQUENCE:
    push_item_writer(emitter, get_current_state(emitter));
    break;
  case STATE_STARTFRAGMENTS:
    push_fragments_writer(emitter, get_current_state(emitter));
    break;
  case STATE_ENDSEQUENCE:
    pop_item_writer(emitter, get_current_state(emitter));
    break;
  default:;
  }

  return ret;
}

int dicm_emitter_emit(struct dicm_emitter *self, const int event_type) {
  assert(event_type >= 0);
  struct _emitter *emitter = (struct _emitter *)self;
  const enum dicm_event_type next = event_type;
  return dicm_emitter_emit_next(emitter, next);
}

int dicm_emitter_set_key(struct dicm_emitter *self_,
                         const struct dicm_key *key) {
  struct _emitter *emitter = (struct _emitter *)self_;
  const enum dicm_state current_state = get_current_state(emitter);
  assert(current_state == STATE_STARTDOCUMENT || current_state == STATE_VALUE ||
         current_state == STATE_STARTITEM ||
         current_state == STATE_ENDSEQUENCE);

  struct _item_writer *item_writer = get_item_writer(emitter);
  const enum dicm_event_type next = DICM_KEY_EVENT;
  const enum dicm_token token = event2token(next);
  assert(token == TOKEN_KEY);
  struct _attribute *da = &item_writer->da;
  da->tag = key->tag;
  da->vr = key->vr;
  da->vl = 0;

  return 0;
}

int dicm_emitter_set_value_length(struct dicm_emitter *self_,
                                  const uint32_t *len) {
  struct _emitter *emitter = (struct _emitter *)self_;
  const enum dicm_state current_state = get_current_state(emitter);
  assert(current_state == STATE_KEY || current_state == STATE_FRAGMENT);

  struct _item_writer *item_writer = get_item_writer(emitter);
  struct _attribute *da = &item_writer->da;
  da->vl = *len;
  item_writer->value_length_pos = VL_UNDEFINED;

  return 0;
}

int dicm_emitter_write_value(struct dicm_emitter *self_, const void *ptr,
                             size_t len) {
  struct _emitter *emitter = (struct _emitter *)self_;
  const enum dicm_state current_state = get_current_state(emitter);
  struct _item_writer *item_writer = get_item_writer(emitter);
  assert(current_state == STATE_KEY || current_state == STATE_FRAGMENT);

  const uint32_t value_length = item_writer->da.vl;
  assert(len <= value_length);
  const uint32_t to_write = len;
  struct dicm_dst *dst = emitter->dst;
  // Write VL
  if (item_writer->value_length_pos == VL_UNDEFINED) {
    union _ude ude;
    const bool is_vr16 = _ude_init(&ude, &item_writer->da);
    const size_t len16 = is_vr16 ? 2u : 4u;
    size_t s = item_writer->da.vl;
    int64_t dlen;
    if (is_vr16) {
      _ede16_set_vl(&ude, s);
      dlen = dicm_dst_write(dst, &ude.ede16.vl16, len16);
    } else {
      _ede32_set_vl(&ude, s);
      dlen = dicm_dst_write(dst, &ude.ede32.vl, len16);
    }
    assert(dlen == (int64_t)len16);

    item_writer->value_length_pos = 0;
  }

  int err = dicm_dst_write(dst, ptr, to_write);
  (void)err;
  item_writer->value_length_pos += to_write;
  assert(item_writer->value_length_pos <= item_writer->da.vl);

  return 0;
}

int _emitter_destroy(struct object *const self) {
  struct _emitter *emitter = (struct _emitter *)self;
  array_free(emitter->item_writers);
  free(emitter);
  return 0;
}

int _ds_writer_next_token(struct _item_writer *self, struct dicm_dst *dst,
                          const enum dicm_event_type next);

int dicm_emitter_create(struct dicm_emitter **pself) {
  struct _emitter *self = (struct _emitter *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->emitter;
    self->emitter.vtable = &g_vtable;

    array_new(item_writer_t, self->item_writers);
    struct _item_writer new_item = {.current_item_state = STATE_INVALID,
                                    .value_length_pos = VL_UNDEFINED,
                                    .fp_next_token = _ds_writer_next_token};
    array_push(self->item_writers, new_item);

    return 0;
  }
  return -1;
}
