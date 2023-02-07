#include "dicm_parser.h"

#include "dicm_item.h"
#include "dicm_src.h"

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc */

// FIXME I need to define a name without spaces:
typedef struct _item_reader item_reader_t;
struct _parser {
  struct dicm_parser parser;
  /* data */
  struct dicm_src *src;

  /* the current item state */
  enum dicm_state current_item_state;

  /* current pos in value_length */
  uint32_t value_length_pos;

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
#if 1
    .parser = {.fp_get_key = _parser_get_key,
               .fp_get_value_length = _parser_get_value_length,
               .fp_read_value = _parser_read_value}
#endif
};

static inline struct _item_reader *get_item_reader(struct _parser *parser) {
  return &array_back(parser->item_readers);
}

static inline enum dicm_state get_state(struct _parser *parser) {
  return parser->current_item_state;
}

static inline bool is_root_dataset(const struct _parser *self) {
  return self->item_readers->size == 1;
}

int dicm_parser_get_key(struct dicm_parser *self, struct dicm_key *key) {
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

int _parser_get_key(struct dicm_parser *const self, struct dicm_key *key) {
  struct _parser *parser = (struct _parser *)self;
  struct _item_reader *item_reader = get_item_reader(parser);
  key->tag = item_reader->da.tag;
  key->vr = item_reader->da.vr;
  return 0;
}

int _parser_get_value_length(struct dicm_parser *const self, uint32_t *len) {
  struct _parser *parser = (struct _parser *)self;
  struct _item_reader *item_reader = get_item_reader(parser);
  *len = item_reader->da.vl;
  return 0;
}

struct _item_reader get_new_reader_ds();
struct _item_reader get_new_implicit_reader_ds();
struct _item_reader get_new_reader_item();
struct _item_reader get_new_reader_frag();

static inline void push_ds_reader(struct _parser *parser,
                                  const enum dicm_state current_state) {
  assert(current_state == STATE_INVALID);
  parser->current_item_state = current_state;

  parser->value_length_pos = VL_UNDEFINED;
  struct _item_reader new_item = get_new_reader_ds();
  array_push(parser->item_readers, new_item);
  assert(is_root_dataset(parser));
}

static inline void
push_ds_implicit_reader(struct _parser *parser,
                        const enum dicm_state current_state) {
  assert(current_state == STATE_INVALID);
  parser->current_item_state = current_state;

  parser->value_length_pos = VL_UNDEFINED;
  struct _item_reader new_item = get_new_implicit_reader_ds();
  array_push(parser->item_readers, new_item);
  assert(is_root_dataset(parser));
}

#define item_reader_next_level(t, state)                                       \
  ((t)->vtable->reader.fp_next_level((t), (state)))

static inline void push_item_reader(struct _parser *parser,
                                    const enum dicm_state current_state) {
  struct _item_reader *item_reader = get_item_reader(parser);
  struct _item_reader new_item =
      item_reader_next_level(item_reader, current_state);
  array_push(parser->item_readers, new_item);
}

static inline void push_fragments_reader(struct _parser *parser) {
  struct _item_reader new_item = get_new_reader_frag();
  array_push(parser->item_readers, new_item);
}

static inline void pop_item_reader(struct _parser *parser) {
  (void)array_pop(parser->item_readers);
}

/* public API */
int dicm_parser_set_input(struct dicm_parser *self, const int structure_type,
                          struct dicm_src *src) {
  struct _parser *parser = (struct _parser *)self;
  // clear any previous run:
  parser->item_readers->size = 0;
  // update ready state:
  parser->src = src;
  enum dicm_state new_state = STATE_INVALID;
  const enum dicm_structure_type estype = structure_type;
  switch (estype) {
  case DICM_STRUCTURE_ENCAPSULATED:
    push_ds_reader(parser, STATE_INVALID);
    new_state = STATE_INIT;
    break;
  case DICM_STRUCTURE_EXPLICIT_LE:
    assert(0);
    new_state = STATE_INIT;
    break;
  case DICM_STRUCTURE_EXPLICIT_BE:
    assert(0);
    new_state = STATE_INIT;
    break;
  case DICM_STRUCTURE_IMPLICIT:
    push_ds_implicit_reader(parser, STATE_INVALID);
    new_state = STATE_INIT;
    break;
  default:;
  }
  parser->current_item_state = new_state;
  return new_state;
}

int _parser_read_value(struct dicm_parser *const self, void *b, size_t s) {
  struct _parser *parser = (struct _parser *)self;
  struct _item_reader *item_reader = get_item_reader(parser);
  const uint32_t value_length = item_reader->da.vl;
  const size_t max_length = s;
  const uint32_t to_read =
      max_length < (size_t)value_length ? (uint32_t)max_length : value_length;

  struct dicm_src *src = parser->src;
  int64_t err = dicm_src_read(src, b, to_read);
  if (err != (int64_t)to_read) {
    parser->current_item_state = STATE_INVALID;
    return -1;
  }
  parser->value_length_pos += to_read;
  assert(parser->value_length_pos <= item_reader->da.vl);

  return 0;
}

#define item_reader_next_event(t, tok, src)                                    \
  ((t)->vtable->reader.fp_next_event((t), (tok), (src)))

int dicm_parser_next_event(struct dicm_parser *self) {
  struct _parser *parser = (struct _parser *)self;

  struct _item_reader *item_reader = get_item_reader(parser);
  // special init case
  // TODO: move to external state machine:
  const enum dicm_state cur_state = get_state(parser);
  if (STATE_INIT == cur_state) {
    assert(parser->src);
    parser->current_item_state = STATE_STARTDOCUMENT;
    return DICM_DOCUMENT_START_EVENT;
  }

  if (STATE_VALUE == cur_state) {
    assert(item_reader->da.vl == parser->value_length_pos);
  }
  // else get next dicm event:
  const enum dicm_state new_state = item_reader_next_event(
      item_reader, parser->current_item_state, parser->src);
  parser->current_item_state = new_state;
  // at this point item_reader->current_item_state has been updated
  if (new_state == STATE_VALUE) {
    parser->value_length_pos = 0;
  }

  // change item_reader based on state:
  switch (get_state(parser)) {
  case STATE_STARTSEQUENCE:
    push_item_reader(parser, STATE_STARTSEQUENCE);
    break;
  case STATE_STARTFRAGMENTS:
    push_fragments_reader(parser);
    break;
  case STATE_ENDSEQUENCE:
    /* item or fragment */
    pop_item_reader(parser);
    break;
  default:;
  }

  if (new_state < 0) {
    return -1;
  }
  const enum dicm_event_type next = state2event(new_state);
  return next;
}

int dicm_parser_create(struct dicm_parser **pself) {
  struct _parser *self = (struct _parser *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->parser;
    self->parser.vtable = &g_vtable;
    array_new(item_reader_t, self->item_readers);

    return 0;
  }
  return -1;
}
