#include "dicm_sequence.h"

static inline bool vl_is_valid(const dicm_vl_t vl) {
  return dicm_vl_is_undefined(vl) || vl % 2 == 0;
}

#define structure_read_key_token(t, src)                                       \
  ((t)->vtable->reader.fp_structure_read_key((t), (src)))

static enum token read_ul_item_end(struct level_parser *const self,
                                   struct dicm_src *src) {
  assert(self->item_length2 == VL_UNDEFINED);
  assert(self->item_length_pos <= self->item_length2);

  const enum token token = structure_read_key_token(self, src);
  return token;
}

static enum token read_dl_item_end(struct level_parser *const self,
                                   struct dicm_src *src) {
  assert(self->item_length2 != VL_UNDEFINED);
  assert(self->item_length_pos <= self->item_length2);

  // no explicit token, synthetic one instead:
  if (self->item_length2 == self->item_length_pos) {
    // update sequence length position here, since read_dual/read_marker is not
    // called
    self->sequence_length_pos += self->item_length_pos;
    assert(self->sequence_length_pos <= self->sequence_length2);

    return TOKEN_ENDITEM;
  }
  const enum token token = structure_read_key_token(self, src);
  if (token == TOKEN_ENDITEM) {
    /* cannot have explicit enditem within defined length item */
    /* FIXME: we are loosing the actual context for proper logging */
    return TOKEN_INVALID_DATA;
  }
  return token;
}

static struct item_read_vtable const g_ul_item_read_vtable = {
    .item = {.fp_end_item_token = read_ul_item_end}};

static struct item_read_vtable const g_dl_item_read_vtable = {
    .item = {.fp_end_item_token = read_dl_item_end}};

enum token sequence_process_delim(struct level_parser *const self,
                                  const uint32_t tag, const uint32_t ide_vl) {
  switch (tag) {
  case TAG_STARTITEM:
    // init cur pos:
    self->item_length_pos = 0;
    // install
    self->item_length2 = ide_vl;
    self->vtable3 = ide_vl == VL_UNDEFINED ? &g_ul_item_read_vtable
                                           : &g_dl_item_read_vtable;
    // update seq length with item start length immediately this will help in
    // handling the case where item length is larger than current level
    // sequence_length
    self->sequence_length_pos += 8;
    assert(self->sequence_length_pos <= self->sequence_length2);
    return vl_is_valid(ide_vl) ? TOKEN_STARTITEM : TOKEN_INVALID_DATA;
  case TAG_ENDITEM:
    // two step process, first update item length:
    // FIXME: 32bits overflow what if inside sequence length ?
    self->item_length_pos += 8;
    assert(self->item_length_pos <= self->item_length2);
    // update sequence length position using item length + end item length:
    self->sequence_length_pos += self->item_length_pos;
    assert(self->sequence_length_pos <= self->sequence_length2);
    // do not reset item length pos here
    return ide_vl == 0 ? TOKEN_ENDITEM : TOKEN_INVALID_DATA;
  case TAG_ENDSQITEM:
    self->sequence_length_pos += 8;
    assert(self->sequence_length_pos <= self->sequence_length2);
    return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
  default:;
  }

  return TOKEN_KEY;
}

static enum token read_ul_sequence_end(struct level_parser *const self,
                                       struct dicm_src *src) {
  assert(self->sequence_length2 == VL_UNDEFINED);
  assert(self->sequence_length_pos <= self->sequence_length2);
  const enum token token = structure_read_key_token(self, src);
  return token;
}

static enum token read_dl_sequence_end(struct level_parser *const self,
                                       struct dicm_src *src) {
  assert(self->sequence_length_pos != VL_UNDEFINED);
  assert(self->sequence_length_pos <= self->sequence_length2);
  if (self->sequence_length2 == self->sequence_length_pos)
    return TOKEN_ENDSQITEM;
  const enum token token = structure_read_key_token(self, src);
  if (token == TOKEN_ENDSQITEM)
    return TOKEN_INVALID_DATA;

  return token;
}

static struct sequence_read_vtable const g_ul_sq_read_vtable = {
    .sequence = {.fp_end_sequence_token = read_ul_sequence_end}};

static struct sequence_read_vtable const g_dl_sq_read_vtable = {
    .sequence = {.fp_end_sequence_token = read_dl_sequence_end}};

void sequence_setup_level(struct level_parser *const new_item) {
  // root item is undefined length sq:
  new_item->sequence_length2 = VL_UNDEFINED;
  new_item->sequence_length_pos = 0;
  new_item->vtable2 = &g_ul_sq_read_vtable;
  // undefined length item:
  new_item->item_length2 = VL_UNDEFINED;
  new_item->item_length_pos = 0;
  new_item->vtable3 = &g_ul_item_read_vtable;
}

void sequence_setup_next_level(const struct level_parser *const self,
                               struct level_parser *const new_item) {
  if (self->da.vl == VL_UNDEFINED) {
    new_item->vtable2 = &g_ul_sq_read_vtable;
    new_item->sequence_length2 = VL_UNDEFINED;
  } else {
    new_item->vtable2 = &g_dl_sq_read_vtable;
    // must repeat the value at the level where items will be read to handle end
    // of sequence
    new_item->sequence_length2 = self->da.vl;
  }
  new_item->sequence_length_pos = 0;
}

/** **/

#define structure_write_key_token(t, dst, tok)                                 \
  ((t)->vtable->level_emitter.fp_structure_key_token((t), (dst), (tok)))

static enum token write_ul_sequence_end(struct level_emitter *const self,
                                        struct dicm_dst *dst,
                                        const enum dicm_event_type next) {
  const enum token token = structure_write_key_token(self, dst, next);
  return token;
}

static enum token write_dl_sequence_end(struct level_emitter *const self,
                                        struct dicm_dst *dst,
                                        const enum dicm_event_type next) {
  assert(next == DICM_SEQUENCE_END_EVENT || next == DICM_ITEM_START_EVENT);
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_ITEM_START_EVENT:
    token = structure_write_key_token(self, dst, next);
    break;
  case DICM_SEQUENCE_END_EVENT:
    assert(self->sequence_length_pos == self->sequence_length2);
    /* defined length sq do not need any marker */
    token = TOKEN_ENDSQITEM;
    break;
  default:
    ASSUME(false);
  }
  return token;
}

static struct sequence_vtable const g_evrle_ul_sq_vtable = {
    .sequence = {.fp_end_sequence_token = write_ul_sequence_end}};

static struct sequence_vtable const g_evrle_dl_sq_vtable = {
    .sequence = {.fp_end_sequence_token = write_dl_sequence_end}};

static enum token write_ul_item_end(struct level_emitter *const self,
                                    struct dicm_dst *dst,
                                    const enum dicm_event_type next) {
  const enum token token = structure_write_key_token(self, dst, next);
  return token;
}

static enum token write_dl_item_end(struct level_emitter *const self,
                                    struct dicm_dst *dst,
                                    const enum dicm_event_type next) {
  assert(self->item_length2 != VL_UNDEFINED);
  assert(self->item_length_pos <= self->item_length2);
  assert(next == DICM_ITEM_END_EVENT || next == DICM_KEY_EVENT);
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_KEY_EVENT:
    token = structure_write_key_token(self, dst, next);
    break;
  case DICM_ITEM_END_EVENT:
    assert(self->item_length2 == self->item_length_pos);
    // reset:
    self->sequence_length_pos += self->item_length_pos;
    self->item_length_pos = 0;
    token = TOKEN_ENDITEM;
    break;
  default:
    ASSUME(false);
  }

  return token;
}

static struct item_vtable const g_evrle_ul_item_vtable = {
    .item = {.fp_end_item_token = write_ul_item_end}};

static struct item_vtable const g_evrle_dl_item_vtable = {
    .item = {.fp_end_item_token = write_dl_item_end}};

void sequence_level_emitter_next_level(const struct level_emitter *const self,
                                       struct level_emitter *const new_item) {
  new_item->item_length2 = VL_UNDEFINED;
  if (self->da.vl == VL_UNDEFINED) {
    new_item->vtable2 = &g_evrle_ul_sq_vtable;
  } else {
    new_item->vtable2 = &g_evrle_dl_sq_vtable;
  }
  new_item->sequence_length2 = self->da.vl;
}

void sequence_init_level_emitter(struct level_emitter *const new_item) {
  new_item->sequence_length2 = VL_UNDEFINED;
  new_item->item_length2 = VL_UNDEFINED;
  new_item->vtable3 = &g_evrle_ul_item_vtable;
}

void init_level_emitter_item(struct level_emitter *new_item) {
  if (new_item->item_length2 == VL_UNDEFINED) {
    new_item->vtable3 = &g_evrle_ul_item_vtable;
  } else {
    new_item->vtable3 = &g_evrle_dl_item_vtable;
  }
}

enum token sequence_process_delim2(struct level_emitter *const self,
                                   const enum dicm_event_type next,
                                   struct ivr *ivr) {
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_ITEM_START_EVENT:
    ivr->tag = TAG_STARTITEM;
    ivr->vl = self->item_length2;
    token = TOKEN_STARTITEM;
    // start item length
    self->item_length_pos = 0;
    // update sequence length
    self->sequence_length_pos += 8;
    break;
  case DICM_ITEM_END_EVENT:
    assert(self->item_length2 == VL_UNDEFINED);
    ivr->tag = TAG_ENDITEM;
    ivr->vl = 0;
    token = TOKEN_ENDITEM;
    // update lengths
    self->item_length_pos += 8;
    self->sequence_length_pos += self->item_length_pos;
    break;
  case DICM_SEQUENCE_END_EVENT:
    assert(self->sequence_length2 == VL_UNDEFINED);
    ivr->tag = TAG_ENDSQITEM;
    ivr->vl = 0;
    token = TOKEN_ENDSQITEM;
    // update sequence length
    self->sequence_length_pos += 8;
    break;
  default:
    ASSUME(false);
  }
  return token;
}
