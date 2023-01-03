#include "dicm_parser.h"

#include "dicm_item.h"
#include "dicm_private.h"
#include "dicm_src.h"

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc */

// FIXME I need to define a name without spaces:
typedef struct _item_reader item_reader_t;
struct _parser {
  struct dicm_parser parser;
  /* data */
  struct dicm_src *src;

  /* the current state */
  enum dicm_state current_state;

  /* item readers */
  array(item_reader_t) * item_readers;
};

static DICM_CHECK_RETURN int _parser_destroy(struct object *) DICM_NONNULL;
static DICM_CHECK_RETURN int _parser_get_key(struct dicm_parser *,
                                             struct dicm_key *) DICM_NONNULL;
static DICM_CHECK_RETURN int _parser_get_value_length(struct dicm_parser *,
                                                      uint32_t *) DICM_NONNULL;
static DICM_CHECK_RETURN int _parser_read_value(struct dicm_parser *, void *,
                                                size_t) DICM_NONNULL;

static struct parser_vtable const g_vtable = {
    /* object interface */
    .object = {.fp_destroy = _parser_destroy},
    /* parser interface */
    .parser = {.fp_get_key = _parser_get_key,
               .fp_get_value_length = _parser_get_value_length,
               .fp_read_value = _parser_read_value}};

int dicm_parser_set_input(struct dicm_parser *self, struct dicm_src *src) {
  assert(src);
  struct _parser *parser = (struct _parser *)self;
  parser->src = src;
  // update state:
  array_back(parser->item_readers).current_item_state = STATE_INIT;
  parser->current_state = STATE_INIT;
  return 0;
}

int dicm_parser_get_key(struct dicm_parser *self, struct dicm_key *key) {
  assert(key);
  return dicm_parser_get_key1(self, key);
}

int dicm_parser_get_value_length(struct dicm_parser *self, uint32_t *len) {
  return dicm_parser_get_value_length1(self, len);
}

int dicm_parser_read_value(struct dicm_parser *self, void *ptr, size_t len) {
  return dicm_parser_read_value1(self, ptr, len);
}

int _parser_destroy(struct object *const self) {
  struct _parser *parser = (struct _parser *)self;
  array_free(parser->item_readers);
  free(parser);
  return 0;
}

// get current item reader
static inline struct _item_reader *
get_item_reader(struct _parser *parser, const enum dicm_state current_state) {
  assert(array_back(parser->item_readers).current_item_state == current_state);
  return &array_back(parser->item_readers);
}

int _parser_get_key(struct dicm_parser *const self_, struct dicm_key *key) {
  struct _parser *self = (struct _parser *)self_;
#if 0
  struct _item_reader *item_reader = &array_back(self->item_readers);
#else
  const enum dicm_state current_state = self->current_state;
  struct _item_reader *item_reader = get_item_reader(self, current_state);
#endif
  key->tag = item_reader->da.tag;
  key->vr = item_reader->da.vr;
  return 0;
}

int _parser_get_value_length(struct dicm_parser *const self_, uint32_t *len) {
  struct _parser *self = (struct _parser *)self_;
#if 0
  struct _item_reader *item_reader = &array_back(self->item_readers);
#else
  const enum dicm_state current_state = self->current_state;
  struct _item_reader *item_reader = get_item_reader(self, current_state);
#endif
  *len = item_reader->da.vl;
  return 0;
}

static inline bool is_root_dataset(const struct _parser *self) {
  return self->item_readers->size == 1;
}

enum dicm_event_type _ds_reader_next_event(struct _item_reader *self,
                                           struct dicm_src *src);
enum dicm_event_type _item_reader_next_event(struct _item_reader *self,
                                             struct dicm_src *src);
enum dicm_event_type _fragments_reader_next_event(struct _item_reader *self,
                                                  struct dicm_src *src);

static inline void push_item_reader(struct _parser *parser,
                                    const enum dicm_state current_state) {
  assert(current_state == STATE_STARTSEQUENCE);
  assert(array_back(parser->item_readers).current_item_state == current_state);

  struct _item_reader new_item = {.current_item_state = current_state,
                                  .fp_next_event = _item_reader_next_event};
  array_push(parser->item_readers, new_item);
}

static inline void push_fragments_reader(struct _parser *parser,
                                         const enum dicm_state current_state) {
  assert(current_state == STATE_STARTFRAGMENTS);
  assert(array_back(parser->item_readers).current_item_state == current_state);

  struct _item_reader new_item = {.current_item_state = current_state,
                                  .fp_next_event =
                                      _fragments_reader_next_event};
  array_push(parser->item_readers, new_item);
}

static inline void pop_item_reader(struct _parser *parser,
                                   const enum dicm_state current_state) {
  assert(array_back(parser->item_readers).current_item_state == current_state);

  (void)array_pop(parser->item_readers);
  struct _item_reader *item_reader = &array_back(parser->item_readers);
  assert(item_reader->current_item_state == STATE_STARTSEQUENCE ||
         item_reader->current_item_state == STATE_STARTFRAGMENTS);
  item_reader->current_item_state = current_state; // re-initialize
}

int _parser_read_value(struct dicm_parser *const self_, void *b, size_t s) {
  struct _parser *self = (struct _parser *)self_;
#if 0
  struct _item_reader *item_reader = &array_back(self->item_readers);
#else
  const enum dicm_state current_state = self->current_state;
  struct _item_reader *item_reader = get_item_reader(self, current_state);
#endif
  const uint32_t value_length = item_reader->da.vl;
  const size_t max_length = s;
  const uint32_t to_read =
      max_length < (size_t)value_length ? (uint32_t)max_length : value_length;

  struct dicm_src *src = self->src;
  int err = dicm_src_read(src, b, to_read);
  (void)err;
  item_reader->value_length_pos += to_read;
  assert(item_reader->value_length_pos <= item_reader->da.vl);

  return 0;
}

int dicm_parser_next_event(struct dicm_parser *self_) {
  struct _parser *self = (struct _parser *)self_;
  const enum dicm_state current_state = self->current_state;

  struct _item_reader *item_reader = get_item_reader(self, current_state);
  // special init case
  // TODO: move to external state machine:
  if (current_state == STATE_INIT) {
    assert(self->src);
    item_reader->current_item_state = STATE_STARTSTREAM;
    self->current_state = item_reader->current_item_state;
    return DICM_STREAM_START_EVENT;
  }
  // else get next dicm event:
  const enum dicm_event_type next =
      item_reader->fp_next_event(item_reader, self->src);
  self->current_state = item_reader->current_item_state;

  // change item_reader based on state:
  switch (self->current_state) {
  case STATE_STARTSEQUENCE:
    push_item_reader(self, self->current_state);
    break;
  case STATE_STARTFRAGMENTS:
    push_fragments_reader(self, self->current_state);
    break;
  case STATE_ENDSEQUENCE:
    pop_item_reader(self, self->current_state);
    break;
  default:;
  }

  return next;
}

int dicm_parser_create(struct dicm_parser **pself) {
  struct _parser *self = (struct _parser *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->parser;
    self->parser.vtable = &g_vtable;

    self->current_state = STATE_INVALID;
    array_new(item_reader_t, self->item_readers);
    struct _item_reader new_item = {.current_item_state = STATE_INVALID,
                                    .fp_next_event = _ds_reader_next_event};
    array_push(self->item_readers, new_item);

    return 0;
  }
  return -1;
}
