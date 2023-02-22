#include "dicm_item.h"

#include "dicm_dst.h"
#include "dicm_sequence.h"
#include "dicm_src.h"

#include <assert.h>

static inline bool _tag_is_valid(const dicm_tag_t tag) {
  // The following cases have been handled by design:
  assert(tag != TAG_STARTITEM && tag != TAG_ENDITEM && tag != TAG_ENDSQITEM);
  // FIXME: valid for DataSet but not CommandSet
  return dicm_tag_get_group(tag) >= 0x0008;
}

static inline bool _tag_is_creator(const dicm_tag_t tag) {
  const uint_fast16_t element = dicm_tag_get_element(tag);
  return dicm_tag_is_private(tag) && (element > 0x0 && element <= 0xff);
}

static inline bool _vr_is_valid(const dicm_vr_t vr) {
  if ((vr & 0xffff0000) != 0x0) {
    return false;
  }
  const char *str = dicm_vr_get_string(vr);
  return str[0] >= 'A' && str[0] <= 'Z' && str[1] >= 'A' && str[1] <= 'Z';
}

static inline bool vl_is_valid(const dicm_vl_t vl) {
  return dicm_vl_is_undefined(vl) || vl % 2 == 0;
}

static inline bool _attribute_is_valid(const struct key_info *da) {
  // 1. check triplet separately:
  const bool valid =
      _tag_is_valid(da->tag) && _vr_is_valid(da->vr) && vl_is_valid(da->vl);
  if (!valid) {
    return false;
  }
  // 2. handle finer cases here:
  if (dicm_vl_is_undefined(da->vl) && da->vr != VR_SQ) {
    if (!dicm_attribute_is_encapsulated_pixel_data(da)) {
      return false;
    }
  }
  if (dicm_tag_is_group_length(da->tag)) {
    if (da->vr != VR_UL)
      return false;
  } else if (_tag_is_creator(da->tag)) {
    if (da->vr != VR_LO)
      return false;
  }
  return true;
}

#define level_parser_key_token(t, src)                                         \
  ((t)->vtable3->item.fp_end_item_token((t), (src)))
#define level_parser_value_token(t, src)                                       \
  ((t)->vtable->reader.fp_value_token((t), (src)))
#define level_parser_delim_token(t, src)                                       \
  ((t)->vtable2->sequence.fp_end_sequence_token((t), (src)))

#define level_emitter_key_token(t, dst, tok)                                   \
  ((t)->vtable3->item.fp_end_item_token((t), (dst), (tok)))
#define level_emitter_value_token(t, dst, tok)                                 \
  ((t)->vtable->level_emitter.fp_value_token((t), (dst), (tok)))
#define level_emitter_delim_token(t, dst, tok)                                 \
  ((t)->vtable2->sequence.fp_end_sequence_token((t), (dst), (tok)))

#define SWAP_TAG(x)                                                            \
  ((((x)&0x000000ff) << 24) | (((x)&0x0000ff00) << 8) |                        \
   (((x)&0x00ff0000) >> 8) | (((x)&0xff000000) >> 24))

static inline uint32_t evrbe2tag(const uint32_t tag_bytes) {
  return bswap_32(tag_bytes);
}

static inline enum token
evrbe_structure_read_marker_impl(struct level_parser *const self,
                                 struct dicm_src *src, struct dual *dual) {
  const int64_t ssize = dicm_src_read(src, &dual->ivr, 8);
  if (ssize != 8) {
    /* special case 0 length read to indicate end of document */
    return ssize == 0 ? TOKEN_EOF : TOKEN_INVALID_DATA;
  }

  const uint32_t tag = evrbe2tag(dual->ivr.tag);
  const uint32_t ide_vl = bswap_32(dual->ivr.vl);
  return sequence_process_delim(self, tag, ide_vl);
}

static enum token evrbe_structure_read_key(struct level_parser *const self,
                                           struct dicm_src *src) {
  struct dual dual;
  const enum token token = evrbe_structure_read_marker_impl(self, src, &dual);
  if (token != TOKEN_KEY)
    return token;

  const uint32_t tag = evrbe2tag(dual.ivr.tag);
  self->da.tag = tag;

  const uint32_t vr = dual.evr.vr16;
  self->da.vr = vr;
  const bool vr16 = _is_vr16(vr);
  uint32_t data_len = 8;
  uint32_t vl;
  if (vr16) {
    vl = bswap_16(dual.evr.vl16);
  } else {
    const int64_t ssize = dicm_src_read(src, &dual.evr.vl32, 4);
    if (ssize != 4) {
      return TOKEN_INVALID_DATA;
    }
    vl = bswap_32(dual.evr.vl32);
    data_len += 4;
  }
  self->da.vl = vl;

  if (!_attribute_is_valid(&self->da)) {
    assert(0);
    return TOKEN_INVALID_DATA;
  }

  // we have the full attribute length, let's update item_length:
  assert(self->item_length_pos != VL_UNDEFINED);
  self->item_length_pos += data_len;
  assert(self->item_length_pos <= self->item_length2);

  return TOKEN_KEY;
}

static enum token evrbe_level_parser_read_value(struct level_parser *self,
                                                struct dicm_src *src) {
  assert(src);
  const dicm_vr_t vr = self->da.vr;
  if (vr == VR_SQ) {
    return TOKEN_STARTSEQUENCE;
  } else {
    assert(!dicm_vl_is_undefined(self->da.vl));
    return TOKEN_VALUE;
  }
}

static enum state evrbe_root_parser_next_event(struct level_parser *self,
                                               const enum state current_state,
                                               struct dicm_src *src) {
  enum token token;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTDOCUMENT: /* enter state */
    token = level_parser_key_token(self, src);
    /* empty document is an error */
    new_state = token == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    break;
  case STATE_KEY:
    token = level_parser_value_token(self, src);
    new_state = token == TOKEN_VALUE
                    ? STATE_VALUE
                    : (token == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                    : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE: // re-enter case
    token = level_parser_key_token(self, src);
    if (token == TOKEN_EOF) {
      new_state = STATE_ENDDOCUMENT;
    } else {
      new_state = token == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    }
    break;
  default:
    ASSUME(false);
  }
  return new_state;
}

static enum state evrbe_level_parser_next_event(struct level_parser *self,
                                                const enum state current_state,
                                                struct dicm_src *src) {
  enum token token;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE: /* enter state */
  case STATE_ENDITEM:
    token = level_parser_delim_token(self, src);
    new_state =
        token == TOKEN_STARTITEM
            ? STATE_STARTITEM
            : (token == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_KEY:
    token = level_parser_value_token(self, src);
    new_state = token == TOKEN_VALUE
                    ? STATE_VALUE
                    : (token == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                    : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_STARTITEM:
  case STATE_ENDSEQUENCE:
    token = level_parser_key_token(self, src);
    new_state = token == TOKEN_KEY
                    ? STATE_KEY
                    : (token == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  default:
    ASSUME(false);
  }
  return new_state;
}

static enum token evrbe_structure_write_key(struct level_emitter *self,
                                            struct dicm_dst *dst,
                                            const enum dicm_event_type next) {
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_KEY_EVENT: {
    const struct evr evr = _evr_init2(&self->da);
    const size_t key_size = evr_get_key_size(evr);
    const int64_t dlen = dicm_dst_write(dst, evr.bytes, key_size);
    self->item_length_pos += (uint32_t)key_size;
    token = dlen == (int64_t)key_size ? TOKEN_KEY : TOKEN_INVALID_DATA;
  } break;
  case DICM_ITEM_START_EVENT:
  case DICM_ITEM_END_EVENT:
  case DICM_SEQUENCE_END_EVENT: {
    struct ivr ivr;
    token = sequence_process_delim2(self, next, &ivr);
    ivr.tag = evrbe2tag(ivr.tag);
    ivr.vl = bswap_32(ivr.vl);
    const int64_t dlen = dicm_dst_write(dst, ivr.bytes, 8);
    token = dlen == 8 ? token : TOKEN_INVALID_DATA;
  } break;
  case DICM_DOCUMENT_END_EVENT:
    token = TOKEN_EOF;
    break;
  default:
    ASSUME(false);
  }
  return token;
}

static enum token
evrbe_level_emitter_write_vl(struct level_emitter *self, struct dicm_dst *dst,
                             const enum dicm_event_type next) {
  assert(next == DICM_VALUE_EVENT);
  const struct evr evr = _evr_init2(&self->da);
  const size_t vl_len = evr.vr_size;
  const int64_t dlen = dicm_dst_write(dst, &evr.vl, vl_len);
  assert(vl_len != VL_UNDEFINED);
  self->item_length_pos += (uint32_t)vl_len;
  return dlen == (int64_t)vl_len ? TOKEN_VALUE : TOKEN_INVALID_DATA;
}

static enum token
evrbe_level_emitter_write_value(struct level_emitter *self,
                                struct dicm_dst *dst,
                                const enum dicm_event_type next) {
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_VALUE_EVENT:
    token = TOKEN_VALUE;
    break;
  case DICM_SEQUENCE_START_EVENT: {
    /* write to dest */
    const uint32_t vl = bswap_32(self->da.vl);
    const int64_t dlen = dicm_dst_write(dst, &vl, 4);
    self->item_length_pos += 4;
    token = dlen == 4 ? TOKEN_STARTSEQUENCE : TOKEN_INVALID_DATA;
  } break;
  default:
    ASSUME(false);
  }

  return token;
}

static enum state evrbe_root_emitter_next_event(
    struct level_emitter *self, const enum state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  enum token token;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTDOCUMENT: /* enter state */
    token = level_emitter_key_token(self, dst, next);
    /* empty document is an error */
    new_state = token == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    break;
  case STATE_KEY:
    token = level_emitter_value_token(self, dst, next);
    new_state = token == TOKEN_VALUE
                    ? STATE_VALUE
                    : (token == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                    : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE:
    token = level_emitter_key_token(self, dst, next);
    if (token == TOKEN_EOF) {
      new_state = STATE_ENDDOCUMENT;
    } else {
      new_state = token == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    }
    break;
  default:
    ASSUME(false);
  }
  return new_state;
}

static enum state evrbe_level_emitter_next_event(
    struct level_emitter *self, const enum state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  enum token token;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE:
  case STATE_ENDITEM:
    token = level_emitter_delim_token(self, dst, next);
    new_state =
        token == TOKEN_STARTITEM
            ? STATE_STARTITEM
            : (token == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_KEY:
    token = level_emitter_value_token(self, dst, next);
    new_state = token == TOKEN_VALUE
                    ? STATE_VALUE
                    : (token == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                    : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_STARTITEM:
  case STATE_ENDSEQUENCE:
    token = level_emitter_key_token(self, dst, next);
    new_state = token == TOKEN_KEY
                    ? STATE_KEY
                    : (token == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  default:
    ASSUME(false);
  }
  return new_state;
}

static struct level_parser
evrbe_level_parser_next_level(struct level_parser *level_parser,
                              const enum state current_state);

static struct level_parser_vtable const evrbe_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_structure_read_key = evrbe_structure_read_key,
               .fp_value_token = evrbe_level_parser_read_value,
               .fp_next_level = evrbe_level_parser_next_level,
               .fp_next_event = evrbe_root_parser_next_event}};
static struct level_parser_vtable const evrbe_item_vtable = {
    /* item reader interface */
    .reader = {.fp_structure_read_key = evrbe_structure_read_key,
               .fp_value_token = evrbe_level_parser_read_value,
               .fp_next_level = evrbe_level_parser_next_level,
               .fp_next_event = evrbe_level_parser_next_event}};

struct level_parser
evrbe_level_parser_next_level(struct level_parser *self,
                              const enum state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct level_parser new_item = {.vtable = &evrbe_item_vtable};
  sequence_setup_next_level(self, &new_item);
  return new_item;
}

struct level_parser get_new_evrbe_reader_ds() {
  struct level_parser new_item = {.vtable = &evrbe_ds_vtable};
  sequence_setup_level(&new_item);
  return new_item;
}

static struct level_emitter
evrbe_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state);

static struct level_emitter_vtable const g_evrbe_root_vtable = {
    /* root emitter interface */
    .level_emitter = {.fp_structure_key_token = evrbe_structure_write_key,
                      .fp_vl_token = evrbe_level_emitter_write_vl,
                      .fp_value_token = evrbe_level_emitter_write_value,
                      .fp_next_level = evrbe_level_emitter_next_level,
                      .fp_next_event = evrbe_root_emitter_next_event}};
static struct level_emitter_vtable const g_evrbe_item_vtable = {
    /* item level emitter interface */
    .level_emitter = {.fp_structure_key_token = evrbe_structure_write_key,
                      .fp_vl_token = evrbe_level_emitter_write_vl,
                      .fp_value_token = evrbe_level_emitter_write_value,
                      .fp_next_level = evrbe_level_emitter_next_level,
                      .fp_next_event = evrbe_level_emitter_next_event}};

struct level_emitter
evrbe_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct level_emitter new_item = {.vtable = &g_evrbe_item_vtable};
  sequence_level_emitter_next_level(self, &new_item);
  return new_item;
}

void evrbe_init_level_emitter(struct level_emitter *const new_item) {
  assert(new_item->da.tag == 0x0);
  new_item->vtable = &g_evrbe_root_vtable;
  sequence_init_level_emitter(new_item);
}
