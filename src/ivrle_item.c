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

static inline bool vl_is_valid(const dicm_vl_t vl) {
  return dicm_vl_is_undefined(vl) || vl % 2 == 0;
}

static inline bool _attribute_is_valid(const struct key_info *da) {
  // 1. check triplet separately:
  const bool valid = _tag_is_valid(da->tag) && vl_is_valid(da->vl);
  if (!valid) {
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

#define SWAP_TAG(x) ((((x)&0x0000ffff) << 16u) | (((x)&0xffff0000) >> 16u))

static inline uint32_t ivrle2tag(const uint32_t tag_bytes) {
  return SWAP_TAG(tag_bytes);
}

static inline enum token
ivrle_structure_read_marker_impl(struct level_parser *const self,
                                 struct dicm_src *src, struct dual *dual) {
  const int64_t ssize = dicm_src_read(src, &dual->ivr, 8);
  if (ssize != 8) {
    /* special case 0 length read to indicate end of document */
    return ssize == 0 ? TOKEN_EOF : TOKEN_INVALID_DATA;
  }

  const uint32_t tag = ivrle2tag(dual->ivr.tag);
  const uint32_t ide_vl = dual->ivr.vl;
  return sequence_process_delim(self, tag, ide_vl);
}

static enum token ivrle_structure_read_key(struct level_parser *self,
                                           struct dicm_src *src) {
  struct dual dual;
  const enum token token = ivrle_structure_read_marker_impl(self, src, &dual);
  if (token != TOKEN_KEY)
    return token;

  const uint32_t tag = ivrle2tag(dual.ivr.tag);
  self->da.tag = tag;

  uint32_t data_len = 8;
  self->da.vr = VR_NONE;
  self->da.vl = dual.ivr.vl;

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

static enum token ivrle_level_parser_read_value(struct level_parser *self,
                                                struct dicm_src *src) {
  assert(src);
  const dicm_vr_t vr = self->da.vr;
  if (dicm_vl_is_undefined(self->da.vl)) {
    assert(vr == VR_NONE);
    return TOKEN_STARTSEQUENCE;
  } else {
    assert(!dicm_vl_is_undefined(self->da.vl));
    return TOKEN_VALUE;
  }
}

static enum state ivrle_root_parser_next_event(struct level_parser *self,
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

static enum state ivrle_level_parser_next_event(struct level_parser *self,
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

static enum token ivrle_structure_write_key(struct level_emitter *self,
                                            struct dicm_dst *dst,
                                            const enum dicm_event_type next) {
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_KEY_EVENT: {
    const struct ivr ivr = _ivr_init1(&self->da);
    const size_t key_size = 4;
    const int64_t dlen = dicm_dst_write(dst, ivr.bytes, 4);
    self->item_length_pos += (uint32_t)key_size;
    token = dlen == (int64_t)key_size ? TOKEN_KEY : TOKEN_INVALID_DATA;
  } break;
  case DICM_ITEM_START_EVENT:
  case DICM_ITEM_END_EVENT:
  case DICM_SEQUENCE_END_EVENT: {
    struct ivr ivr;
    token = sequence_process_delim2(self, next, &ivr);
    ivr.tag = ivrle2tag(ivr.tag);
    ivr.vl = ivr.vl;
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
ivrle_level_emitter_write_vl(struct level_emitter *self, struct dicm_dst *dst,
                             const enum dicm_event_type next) {
  assert(next == DICM_VALUE_EVENT);
  const struct ivr ivr = _ivr_init1(&self->da);
  const size_t vl_len = 4u;
  const int64_t dlen = dicm_dst_write(dst, &ivr.vl, vl_len);
  assert(dlen == (int64_t)vl_len);
  self->item_length_pos += (uint32_t)vl_len;
  return dlen == (int64_t)vl_len ? TOKEN_VALUE : TOKEN_INVALID_DATA;
}

static enum token
ivrle_level_emitter_write_value(struct level_emitter *self,
                                struct dicm_dst *dst,
                                const enum dicm_event_type next) {
  enum token token = TOKEN_INVALID_DATA;
  switch (next) {
  case DICM_VALUE_EVENT:
    token = TOKEN_VALUE;
    break;
  case DICM_SEQUENCE_START_EVENT: {
    /* write to dest */
    const uint32_t vl = self->da.vl;
    const int64_t dlen = dicm_dst_write(dst, &vl, 4);
    self->item_length_pos += 4;
    token = dlen == 4 ? TOKEN_STARTSEQUENCE : TOKEN_INVALID_DATA;
  } break;
  default:
    ASSUME(false);
  }

  return token;
}

static enum state ivrle_root_emitter_next_event(
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

static enum state ivrle_level_emitter_next_event(
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
ivrle_level_parser_next_level(struct level_parser *level_parser,
                              const enum state current_state);

static struct level_parser_vtable const ivrle_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_structure_read_key = ivrle_structure_read_key,
               .fp_value_token = ivrle_level_parser_read_value,
               .fp_next_level = ivrle_level_parser_next_level,
               .fp_next_event = ivrle_root_parser_next_event}};
static struct level_parser_vtable const ivrle_item_vtable = {
    /* item reader interface */
    .reader = {.fp_structure_read_key = ivrle_structure_read_key,
               .fp_value_token = ivrle_level_parser_read_value,
               .fp_next_level = ivrle_level_parser_next_level,
               .fp_next_event = ivrle_level_parser_next_event}};

struct level_parser
ivrle_level_parser_next_level(struct level_parser *self,
                              const enum state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct level_parser new_item = {.vtable = &ivrle_item_vtable};
  sequence_setup_next_level(self, &new_item);
  return new_item;
}

struct level_parser get_new_ivrle_reader_ds() {
  struct level_parser new_item = {.vtable = &ivrle_ds_vtable};
  sequence_setup_level(&new_item);
  return new_item;
}

static struct level_emitter
ivrle_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state);

static struct level_emitter_vtable const g_ivrle_root_vtable = {
    /* root emitter interface */
    .level_emitter = {.fp_structure_key_token = ivrle_structure_write_key,
                      .fp_vl_token = ivrle_level_emitter_write_vl,
                      .fp_value_token = ivrle_level_emitter_write_value,
                      .fp_next_level = ivrle_level_emitter_next_level,
                      .fp_next_event = ivrle_root_emitter_next_event}};
static struct level_emitter_vtable const g_ivrle_item_vtable = {
    /* item level emitter interface */
    .level_emitter = {.fp_structure_key_token = ivrle_structure_write_key,
                      .fp_vl_token = ivrle_level_emitter_write_vl,
                      .fp_value_token = ivrle_level_emitter_write_value,
                      .fp_next_level = ivrle_level_emitter_next_level,
                      .fp_next_event = ivrle_level_emitter_next_event}};

struct level_emitter
ivrle_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct level_emitter new_item = {.vtable = &g_ivrle_item_vtable};
  sequence_level_emitter_next_level(self, &new_item);
  return new_item;
}

void ivrle_init_level_emitter(struct level_emitter *new_item) {
  assert(new_item->da.tag == 0x0);
  new_item->vtable = &g_ivrle_root_vtable;
  sequence_init_level_emitter(new_item);
}
