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
                                                      size_t *) DICM_NONNULL;
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
  struct _parser *parser = (struct _parser *)self;
  parser->src = src;
  return 0;
}

static inline enum dicm_event_type
token2event(const enum dicm_token dicm_next) {
  enum dicm_event_type next;
  switch (dicm_next) {
  case TOKEN_KEY:
    next = DICM_ELEMENT_KEY_EVENT;
    break;
  case TOKEN_VALUE:
    next = DICM_ELEMENT_VALUE_EVENT;
    break;
  case TOKEN_FRAGMENT:
    next = DICM_FRAGMENT_EVENT;
    break;
  case TOKEN_STARTSEQUENCE:
  case TOKEN_STARTFRAGMENTS:
    next = DICM_SEQUENCE_START_EVENT;
    break;
  case TOKEN_ENDSQITEM:
    next = DICM_SEQUENCE_END_EVENT;
    break;
  case TOKEN_STARTITEM:
    next = DICM_ITEM_START_EVENT;
    break;
  case TOKEN_ENDITEM:
    next = DICM_ITEM_END_EVENT;
    break;
  case TOKEN_EOF:
    next = DICM_DATASET_END_EVENT;
    break;
  default:
    assert(0);
  }
  return next;
}

#if 0
int dicm_parser_delete(struct dicm_parser *self) {
  return object_destroy(self);
}
#endif

int dicm_parser_get_key(struct dicm_parser *self, struct dicm_key *key) {
  return dicm_parser_get_key1(self, key);
}

int dicm_parser_get_value_length(struct dicm_parser *self, size_t *len) {
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

int _parser_get_key(struct dicm_parser *const self, struct dicm_key *key) {
  struct _parser *parser = (struct _parser *)self;
  struct _item_reader *item_reader = &array_back(parser->item_readers);
  key->tag = item_reader->da.tag;
  key->vr = item_reader->da.vr;
  return 0;
}
int _parser_get_value_length(struct dicm_parser *const self, size_t *len) {
  struct _parser *parser = (struct _parser *)self;
  struct _item_reader *item_reader = &array_back(parser->item_readers);
  *len = item_reader->da.vl;
  return 0;
}

static inline bool is_root_dataset(const struct _parser *self) {
  return self->item_readers->size == 1;
}

// get current item reader
static inline struct _item_reader *
get_item_reader(struct _parser *parser, const enum dicm_state current_state) {
  assert(array_back(parser->item_readers).current_item_state == current_state);
  return &array_back(parser->item_readers);
}

int _ds_reader_next_event(struct _item_reader *self, struct dicm_src *src);
int _item_reader_next_event(struct _item_reader *self, struct dicm_src *src);
int _fragments_reader_next_event(struct _item_reader *self,
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
  struct _item_reader *item_reader = &array_back(self->item_readers);
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

  // special init case
  if (current_state == STATE_INIT) {
    self->current_state = STATE_STARTSTREAM;
    return DICM_STREAM_START_EVENT;
  } else if (current_state == STATE_STARTSTREAM) {
    self->current_state = STATE_STARTDATASET;
    return DICM_DATASET_START_EVENT;
  } else if (current_state == STATE_ENDDATASET) {
    self->current_state = STATE_INVALID;
    return DICM_STREAM_END_EVENT;
  }
  // else get next dicm token:
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    push_item_reader(self, current_state);
    break;
  case STATE_STARTFRAGMENTS:
    push_fragments_reader(self, current_state);
    break;
  case STATE_ENDSEQUENCE:
    pop_item_reader(self, current_state);
    break;
  default:;
  }
  struct _item_reader *item_reader = get_item_reader(self, current_state);
  const enum dicm_token dicm_next =
      item_reader->fp_next_event(item_reader, self->src);
  const enum dicm_event_type next = token2event(dicm_next);
  self->current_state = item_reader->current_item_state;

  return next;
}

int dicm_parser_create(struct dicm_parser **pself) {
  struct _parser *self = (struct _parser *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->parser;
    self->parser.vtable = &g_vtable;

    self->current_state = STATE_INIT;
    array_new(item_reader_t, self->item_readers);
    struct _item_reader new_item = {.current_item_state = STATE_STARTDATASET,
                                    .fp_next_event = _ds_reader_next_event};
    array_push(self->item_readers, new_item);

    return 0;
  }
  return -1;
}
