#include "dicm_parser.h"

#include "dicm_item.h"
#include "dicm_src.h"

#include <assert.h> /* assert */
#include <stdlib.h> /* malloc */

// FIXME I need to define a name without spaces:
typedef struct level_parser level_parser_t;
struct parser {
  struct dicm_parser parser;
  /* data */
  struct dicm_src *src;

  /* the current item state */
  enum state current_item_state;

  /* current pos in value_length */
  uint32_t value_length_pos;

  /* level parsers */
  array(level_parser_t) * level_parsers;
};

static DICM_CHECK_RETURN int parser_destroy(struct object *) DICM_NONNULL();
static DICM_CHECK_RETURN int parser_get_key(struct dicm_parser *,
                                            struct dicm_key *) DICM_NONNULL();
static DICM_CHECK_RETURN int parser_get_value_length(struct dicm_parser *,
                                                     uint32_t *) DICM_NONNULL();
static DICM_CHECK_RETURN int parser_read_value(struct dicm_parser *, void *,
                                               size_t) DICM_NONNULL();

static struct parser_vtable const g_vtable = {
    /* object interface */
    .object = {.fp_destroy = parser_destroy},
/* parser interface */
#if 0
    .parser = {.fp_get_key = _parser_get_key,
               .fp_get_value_length = _parser_get_value_length,
               .fp_read_value = _parser_read_value}
#endif
};

static inline struct level_parser *
parser_get_level_parser(struct parser *parser) {
  return &array_back(parser->level_parsers);
}

static inline enum state parser_get_state(struct parser *parser) {
  return parser->current_item_state;
}

static inline bool parser_is_root_dataset(const struct parser *self) {
  return self->level_parsers->size == 1;
}

int dicm_parser_get_key(struct dicm_parser *self, struct dicm_key *key) {
  struct parser *parser = (struct parser *)self;
  const enum state cur_state = parser_get_state(parser);
  if (cur_state == STATE_KEY) {
#if 0
  return dicm_parser_get_key1(self, key);
#else
    struct level_parser *level_parser = parser_get_level_parser(parser);
    key->tag = level_parser->da.tag;
    key->vr = level_parser->da.vr;
    return 0;
#endif
  }
  return -1;
}

int dicm_parser_get_size(struct dicm_parser *self, uint32_t *len) {
  struct parser *parser = (struct parser *)self;
  const enum state cur_state = parser_get_state(parser);
  struct level_parser *level_parser = parser_get_level_parser(parser);
  switch (cur_state) {
  case STATE_STARTITEM:
    *len = level_parser->item_length2;
    return 0;
  case STATE_VALUE:
  case STATE_STARTSEQUENCE:
    *len = level_parser->da.vl;
    return 0;
  case STATE_STARTDOCUMENT:
  case STATE_ENDDOCUMENT:
  case STATE_ENDSEQUENCE:
  case STATE_ENDITEM:
    break;
  case STATE_STARTFRAGMENTS:
    *len = level_parser->da.vl;
    //*len = level_parser->item_length2;
    return 0;
  case STATE_FRAGMENT:
    break;
  default:
    assert(0);
  }
  return -1;
}

int dicm_parser_read_bytes(struct dicm_parser *self, void *ptr, size_t len) {
  struct parser *parser = (struct parser *)self;
  const enum state cur_state = parser_get_state(parser);
  if (cur_state == STATE_VALUE) {
#if 0
  return dicm_parser_read_value1(self, ptr, len);
#else
    return parser_read_value(self, ptr, len);
#endif
  }
  return -1;
}

int parser_destroy(struct object *const self) {
  struct parser *parser = (struct parser *)self;
  array_free(parser->level_parsers);
  free(parser);
  return 0;
}

int parser_get_key(struct dicm_parser *const self, struct dicm_key *key) {
  struct parser *parser = (struct parser *)self;
  struct level_parser *level_parser = parser_get_level_parser(parser);
  key->tag = level_parser->da.tag;
  key->vr = level_parser->da.vr;
  return 0;
}

int parser_get_value_length(struct dicm_parser *const self, uint32_t *len) {
  struct parser *parser = (struct parser *)self;
  struct level_parser *level_parser = parser_get_level_parser(parser);
  *len = level_parser->da.vl;
  return 0;
}

struct level_parser get_new_reader_ds();
struct level_parser get_new_ivrle_reader_ds();
struct level_parser get_new_evrle_reader_ds();
struct level_parser get_new_evrbe_reader_ds();
struct level_parser get_new_reader_item();
struct level_parser get_new_reader_frag();

static inline void push_ds_reader(struct parser *parser,
                                  const enum state current_state) {
  assert(current_state == STATE_INVALID);
  parser->current_item_state = current_state;

  parser->value_length_pos = VL_UNDEFINED;
  struct level_parser new_item = get_new_reader_ds();
  array_push(parser->level_parsers, new_item);
  assert(parser_is_root_dataset(parser));
}

static inline void push_ds_implicit_reader(struct parser *parser,
                                           const enum state current_state) {
  assert(current_state == STATE_INVALID);
  parser->current_item_state = current_state;

  parser->value_length_pos = VL_UNDEFINED;
  struct level_parser new_item = get_new_ivrle_reader_ds();
  array_push(parser->level_parsers, new_item);
  assert(parser_is_root_dataset(parser));
}

static inline void push_ds_explicit_reader(struct parser *parser,
                                           const enum state current_state) {
  assert(current_state == STATE_INVALID);
  parser->current_item_state = current_state;

  parser->value_length_pos = VL_UNDEFINED;
  struct level_parser new_item = get_new_evrle_reader_ds();
  array_push(parser->level_parsers, new_item);
  assert(parser_is_root_dataset(parser));
}

static inline void push_ds_evrbe_reader(struct parser *parser,
                                        const enum state current_state) {
  assert(current_state == STATE_INVALID);
  parser->current_item_state = current_state;

  parser->value_length_pos = VL_UNDEFINED;
  struct level_parser new_item = get_new_evrbe_reader_ds();
  array_push(parser->level_parsers, new_item);
  assert(parser_is_root_dataset(parser));
}

#define level_parser_next_level(t, state)                                      \
  ((t)->vtable->reader.fp_next_level((t), (state)))

static inline void push_level_parser(struct parser *parser,
                                     const enum state current_state) {
  struct level_parser *level_parser = parser_get_level_parser(parser);
  struct level_parser new_item =
      level_parser_next_level(level_parser, current_state);
  array_push(parser->level_parsers, new_item);
}

static inline void push_fragments_reader(struct parser *parser) {
  struct level_parser new_item = get_new_reader_frag();
  array_push(parser->level_parsers, new_item);
}

static inline void pop_level_parser(struct parser *parser) {
  struct level_parser *level_parser_old = parser_get_level_parser(parser);
  (void)array_pop(parser->level_parsers);
  struct level_parser *level_parser = parser_get_level_parser(parser);
  level_parser->item_length_pos += level_parser_old->sequence_length_pos + 0;
}

/* public API */
int dicm_parser_set_input(struct dicm_parser *self, const int structure_type,
                          struct dicm_src *src) {
  struct parser *parser = (struct parser *)self;
  // clear any previous run:
  parser->level_parsers->size = 0;
  // update ready state:
  parser->src = src;
  enum state new_state = STATE_INVALID;
  const enum dicm_structure_type estype = structure_type;
  switch (estype) {
  case DICM_STRUCTURE_ENCAPSULATED:
    push_ds_reader(parser, STATE_INVALID);
    new_state = STATE_INIT;
    break;
  case DICM_STRUCTURE_EXPLICIT_LE:
    push_ds_explicit_reader(parser, STATE_INVALID);
    new_state = STATE_INIT;
    break;
  case DICM_STRUCTURE_EXPLICIT_BE:
    push_ds_evrbe_reader(parser, STATE_INVALID);
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

int parser_read_value(struct dicm_parser *const self, void *b, size_t s) {
  assert(is_aligned(b, 2U));
  struct parser *parser = (struct parser *)self;
  struct level_parser *level_parser = parser_get_level_parser(parser);
  const uint32_t value_length = level_parser->da.vl;
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
  level_parser->item_length_pos += to_read;
  assert(parser->value_length_pos <= level_parser->da.vl);

  return 0;
}

#define level_parser_next_event(t, tok, src)                                   \
  ((t)->vtable->reader.fp_next_event((t), (tok), (src)))

int dicm_parser_next_event(struct dicm_parser *self) {
  struct parser *parser = (struct parser *)self;

  // special init case
  // TODO: move to external state machine:
  const enum state cur_state = parser_get_state(parser);
  if (STATE_INIT == cur_state) {
    assert(parser->src);
    parser->current_item_state = STATE_STARTDOCUMENT;
    return DICM_DOCUMENT_START_EVENT;
  }

  // change level_parser based on state, user is done with get_key/get_size
  // calls
  switch (cur_state) {
  case STATE_STARTSEQUENCE:
    push_level_parser(parser, STATE_STARTSEQUENCE);
    break;
  case STATE_STARTFRAGMENTS:
    push_fragments_reader(parser);
    break;
  case STATE_ENDSEQUENCE:
    /* item or fragment */
    pop_level_parser(parser);
    break;
  default:;
  }

  struct level_parser *level_parser = parser_get_level_parser(parser);
  if (STATE_VALUE == cur_state) {
    assert(level_parser->da.vl == parser->value_length_pos);
  }

  // else get next dicm event:
  const enum state new_state = level_parser_next_event(
      level_parser, parser->current_item_state, parser->src);
  // do not change level in item array so that get_key/get_size are done at
  // proper level
  parser->current_item_state = new_state;
  // at this point level_parser->current_item_state has been updated
  if (new_state == STATE_VALUE) {
    parser->value_length_pos = 0;
  }

  if (new_state < 0) {
    return -1;
  }
  const enum dicm_event_type next = state2event(new_state);
  return next;
}

int dicm_parser_create(struct dicm_parser **pself) {
  struct parser *self = (struct parser *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->parser;
    self->parser.vtable = &g_vtable;
    array_new(level_parser_t, self->level_parsers);

    return 0;
  }
  return -1;
}
