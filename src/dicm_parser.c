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
    .parser = {.fp_get_key = _parser_get_key,
               .fp_get_value_length = _parser_get_value_length,
               .fp_read_value = _parser_read_value}};

static inline enum dicm_state get_current_state(struct _parser *parser) {
  return array_back(parser->item_readers).current_item_state;
}

// get current item reader
static inline struct _item_reader *get_item_reader(struct _parser *parser) {
  return &array_back(parser->item_readers);
}

int dicm_parser_set_input(struct dicm_parser *self, int structure_type,
                          struct dicm_src *src) {
  struct _parser *parser = (struct _parser *)self;
  const enum dicm_structure_type estype = structure_type;
  // update ready state:
  struct _item_reader *item_reader = get_item_reader(parser);
  parser->src = src;
  int ret = 0;
  switch (estype) {
  case DICM_STRUCTURE_ENCAPSULATED:
    item_reader->current_item_state = STATE_INIT_E;
    break;
  case DICM_STRUCTURE_EXPLICIT_LE:
    item_reader->current_item_state = STATE_INIT_L;
    break;
  case DICM_STRUCTURE_EXPLICIT_BE:
    item_reader->current_item_state = STATE_INIT_B;
    break;
  case DICM_STRUCTURE_IMPLICT:
    item_reader->current_item_state = STATE_INIT_I;
    break;
  default:
    item_reader->current_item_state = STATE_INVALID;
    parser->src = NULL;
    ret = -1;
    break;
  }
  return ret;
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

static inline bool is_root_dataset(const struct _parser *self) {
  return self->item_readers->size == 1;
}

enum dicm_token _item_reader_next_impl(struct _item_reader *self,
                                       struct dicm_src *src);
enum dicm_token _item_reader_next_impl2(struct _item_reader *self);
enum dicm_token _fragments_reader_next_impl(struct _item_reader *self,
                                            struct dicm_src *src);
enum dicm_token _fragments_reader_next_impl2(struct _item_reader *self);
enum dicm_event_type _ds_reader_next_event(struct _item_reader *self,
                                           struct dicm_src *src);
enum dicm_event_type _item_reader_next_event(struct _item_reader *self,
                                             struct dicm_src *src);
enum dicm_event_type _fragments_reader_next_event(struct _item_reader *self,
                                                  struct dicm_src *src);

static struct _item_reader_vtable const g_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_key_token = _item_reader_next_impl,
               .fp_value_token = _item_reader_next_impl2,
               .fp_next_event = _ds_reader_next_event}};
static struct _item_reader_vtable const g_item_vtable = {
    /* item reader interface */
    .reader = {.fp_key_token = _item_reader_next_impl,
               .fp_value_token = _item_reader_next_impl2,
               .fp_next_event = _item_reader_next_event}};
static struct _item_reader_vtable const g_fragment_vtable = {
    /* fragment reader interface */
    .reader = {.fp_key_token = _fragments_reader_next_impl,
               .fp_value_token = _fragments_reader_next_impl2,
               .fp_next_event = _fragments_reader_next_event}};

static inline void push_item_reader(struct _parser *parser,
                                    const enum dicm_state current_state) {
  assert(current_state == STATE_STARTSEQUENCE);
  assert(array_back(parser->item_readers).current_item_state == current_state);

  struct _item_reader new_item = {.current_item_state = current_state,
                                  .vtable = &g_item_vtable};
  array_push(parser->item_readers, new_item);
}

static inline void push_fragments_reader(struct _parser *parser,
                                         const enum dicm_state current_state) {
  assert(current_state == STATE_STARTFRAGMENTS);
  assert(array_back(parser->item_readers).current_item_state == current_state);

  struct _item_reader new_item = {.current_item_state = current_state,
                                  .vtable = &g_fragment_vtable};
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
    return -1;
  }
#if 0
  item_reader->value_length_pos += to_read;
  assert(item_reader->value_length_pos <= item_reader->da.vl);
#else
  parser->value_length_pos += to_read;
  assert(parser->value_length_pos <= item_reader->da.vl);
#endif

  return 0;
}

#define item_reader_next_event(t, src)                                         \
  ((t)->vtable->reader.fp_next_event((t), (src)))

int dicm_parser_next_event(struct dicm_parser *self) {
  struct _parser *parser = (struct _parser *)self;

  struct _item_reader *item_reader = get_item_reader(parser);
  // special init case
  // TODO: move to external state machine:
  const enum dicm_state cur_state = get_current_state(parser);
  if (STATE_INIT_E == cur_state) {
    assert(parser->src);
    item_reader->current_item_state = STATE_STARTSTREAM;
    return DICM_STREAM_START_EVENT;
#if 0
  } else if (STATE_STARTSTREAM == cur_state) {
    if (!dicm_src_peek(parser->src))
      return DICM_STREAM_END_EVENT;
    item_reader->current_item_state = STATE_STARTDOCUMENT;
    return DICM_DOCUMENT_START_EVENT;
#endif
  } else if (STATE_ENDDOCUMENT == cur_state) {
#if 0
    assert(!dicm_src_peek(parser->src));
#endif
    item_reader->current_item_state = STATE_ENDSTREAM;
    return DICM_STREAM_END_EVENT;
  }

  if (STATE_VALUE == cur_state) {
    assert(item_reader->da.vl == parser->value_length_pos);
  }
  // else get next dicm event:
  const enum dicm_event_type next =
      item_reader_next_event(item_reader, parser->src);
  // at this point item_reader->current_item_state has been updated
  if (next == DICM_VALUE_EVENT) {
    parser->value_length_pos = 0;
  }

  // change item_reader based on state:
  switch (get_current_state(parser)) {
  case STATE_STARTSEQUENCE:
    push_item_reader(parser, get_current_state(parser));
    break;
  case STATE_STARTFRAGMENTS:
    push_fragments_reader(parser, get_current_state(parser));
    break;
  case STATE_ENDSEQUENCE:
    /* item or fragment */
    pop_item_reader(parser, get_current_state(parser));
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

    // init with invalid state:
    array_new(item_reader_t, self->item_readers);
    struct _item_reader new_item = {.current_item_state = STATE_INVALID,
                                    .vtable = &g_ds_vtable};
    array_push(self->item_readers, new_item);

    return 0;
  }
  return -1;
}
