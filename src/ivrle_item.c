#include "dicm_item.h"

#include "dicm_dst.h"
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
  ((t)->vtable->reader.fp_key_token((t), (src)))
#define level_parser_value_token(t, src)                                       \
  ((t)->vtable->reader.fp_value_token((t), (src)))

#define level_emitter_key_token(t, dst, tok)                                   \
  ((t)->vtable->level_emitter.fp_key_token((t), (dst), (tok)))
#define level_emitter_value_token(t, dst, tok)                                 \
  ((t)->vtable->level_emitter.fp_value_token((t), (dst), (tok)))

#define SWAP_TAG(x) ((((x)&0x0000ffff) << 16u) | (((x)&0xffff0000) >> 16u))

static inline uint32_t ivrle2tag(const uint32_t tag_bytes) {
  return SWAP_TAG(tag_bytes);
}

enum IVRLE_SPECIAL_TAGS {
  IVRLE_TAG_STARTITEM = SWAP_TAG(TAG_STARTITEM),
  IVRLE_TAG_ENDITEM = SWAP_TAG(TAG_ENDITEM),
  IVRLE_TAG_ENDSQITEM = SWAP_TAG(TAG_ENDSQITEM),
};

static const struct ivr ivrle_start_item = {.tag = IVRLE_TAG_STARTITEM,
                                            .vl = VL_UNDEFINED};
static const struct ivr ivrle_end_item = {.tag = IVRLE_TAG_ENDITEM, .vl = 0};
static const struct ivr ivrle_end_sq_item = {.tag = IVRLE_TAG_ENDSQITEM,
                                             .vl = 0};

static enum token ivrle_level_parser_read_key(struct level_parser *self,
                                              struct dicm_src *src) {
  struct dual dual;
  int64_t ssize = dicm_src_read(src, &dual.ivr, 8);
  if (ssize != 8) {
    return ssize == 0 ? TOKEN_EOF : TOKEN_INVALID_DATA;
  }

  const uint32_t tag = ivrle2tag(dual.ivr.tag);
  self->da.tag = tag;
  {
    const uint32_t ide_vl = dual.ivr.vl;
    self->da.vl = ide_vl;
    self->da.vr = VR_NONE;
    switch (tag) {
    case TAG_STARTITEM:
      /* FIXME: no bswap needed at this point */
      assert(dual.ivr.tag == IVRLE_TAG_STARTITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return vl_is_valid(ide_vl) ? TOKEN_STARTITEM : TOKEN_INVALID_DATA;
    case TAG_ENDITEM:
      assert(dual.ivr.tag == IVRLE_TAG_ENDITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDITEM : TOKEN_INVALID_DATA;
    case TAG_ENDSQITEM:
      assert(dual.ivr.tag == IVRLE_TAG_ENDSQITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
    }
  }

  if (!_attribute_is_valid(&self->da)) {
    assert(0);
    return TOKEN_INVALID_DATA;
  }

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

static enum state ivrle_ds_reader_next_event(struct level_parser *self,
                                             const enum state current_state,
                                             struct dicm_src *src) {
  enum token next;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTDOCUMENT: /* enter state */
    next = level_parser_key_token(self, src);
    /* empty document is an error */
    new_state = next == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    break;
  case STATE_KEY:
    next = level_parser_value_token(self, src);
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE);
    new_state = next == TOKEN_VALUE
                    ? STATE_VALUE
                    : (next == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                   : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE: // re-enter case
    next = level_parser_key_token(self, src);
    if (next == TOKEN_EOF) {
      new_state = STATE_ENDDOCUMENT;
    } else {
      assert(next == TOKEN_KEY);
      new_state = next == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    }
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum state ivrle_level_parser_next_event(struct level_parser *self,
                                                const enum state current_state,
                                                struct dicm_src *src) {
  enum token next;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE: /* enter state */
  case STATE_ENDITEM:
    next = level_parser_key_token(self, src);
    assert(next == TOKEN_STARTITEM || next == TOKEN_ENDSQITEM);
    new_state =
        next == TOKEN_STARTITEM
            ? STATE_STARTITEM
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_KEY:
    next = level_parser_value_token(self, src);
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE);
    new_state = next == TOKEN_VALUE
                    ? STATE_VALUE
                    : (next == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                   : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE:
  case STATE_STARTITEM:
    next = level_parser_key_token(self, src);
    assert(next == TOKEN_KEY || next == TOKEN_ENDITEM);
    new_state = next == TOKEN_KEY
                    ? STATE_KEY
                    : (next == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  default:;
  }
  return new_state;
}

static enum state ivrle_level_emitter_write_key(struct level_emitter *self,
                                                struct dicm_dst *dst,
                                                const enum token token) {
  enum state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_KEY: {
    const struct ivr ivr = _ivr_init1(&self->da);
    dlen = dicm_dst_write(dst, ivr.bytes, 4);
    new_state = dlen == 4 ? STATE_KEY : STATE_INVALID;
  } break;
  case TOKEN_STARTITEM:
    /* FIXME: only undef sequence for now */
    dlen = dicm_dst_write(dst, ivrle_start_item.bytes, 8);
    new_state = dlen == 8 ? STATE_STARTITEM : STATE_INVALID;
    break;
  case TOKEN_ENDITEM:
    dlen = dicm_dst_write(dst, ivrle_end_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDITEM : STATE_INVALID;
    break;
  case TOKEN_ENDSQITEM:
    dlen = dicm_dst_write(dst, ivrle_end_sq_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDSEQUENCE : STATE_INVALID;
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum state ivrle_level_emitter_write_vl(struct level_emitter *self,
                                               struct dicm_dst *dst,
                                               const enum token token) {
  assert(token == TOKEN_VALUE);
  const size_t vl_len = 4u;
  const struct ivr ivr = _ivr_init1(&self->da);
  const int64_t dlen = dicm_dst_write(dst, &ivr.vl, vl_len);
  assert(dlen == (int64_t)vl_len);

  return dlen == (int64_t)vl_len ? STATE_VALUE : STATE_INVALID;
}

static enum state ivrle_level_emitter_write_value(struct level_emitter *self,
                                                  struct dicm_dst *dst,
                                                  const enum token token) {
  assert(token == TOKEN_VALUE || TOKEN_STARTSEQUENCE);
  enum state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_VALUE:
    new_state = STATE_VALUE;
    break;
  case TOKEN_STARTSEQUENCE:
    /* update internal state */
    self->da.vl = VL_UNDEFINED;
    /* write to dest */
    dlen = dicm_dst_write(dst, &ivrle_start_item.vl, 4);
    new_state = dlen == 4 ? STATE_STARTSEQUENCE : STATE_INVALID;
    break;
  default:;
  }

  return new_state;
}

static enum state ivrle_ds_writer_next_event(struct level_emitter *self,
                                             const enum state current_state,
                                             struct dicm_dst *dst,
                                             const enum dicm_event_type next) {
  const enum token token = event2token(next);
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTDOCUMENT:
    new_state = level_emitter_key_token(self, dst, token);
    assert(new_state == STATE_KEY);
    break;
  case STATE_KEY:
    new_state = level_emitter_value_token(self, dst, token);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE:
    if (token == TOKEN_KEY) {
      new_state = level_emitter_key_token(self, dst, token);
      assert(new_state == STATE_KEY);
    } else {
      assert(token == TOKEN_EOF);
      new_state = STATE_ENDDOCUMENT;
    }
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum state ivrle_level_emitter_next_event(
    struct level_emitter *self, const enum state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  const enum token token = event2token(next);
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE:
  case STATE_ENDITEM:
    new_state = level_emitter_key_token(self, dst, token);
    assert(new_state == STATE_STARTITEM || new_state == STATE_ENDSEQUENCE);
    break;
  case STATE_KEY:
    new_state = level_emitter_value_token(self, dst, token);
    break;
  case STATE_VALUE:
  case STATE_STARTITEM:
  case STATE_ENDSEQUENCE:
    new_state = level_emitter_key_token(self, dst, token);
    assert(new_state == STATE_KEY || new_state == STATE_ENDITEM);
    break;
  default:
    assert(0);
  }
  return new_state;
}

static struct level_parser
ivrle_level_parser_next_level(struct level_parser *level_parser,
                              const enum state current_state);

static struct level_parser_vtable const ivrle_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_key_token = ivrle_level_parser_read_key,
               .fp_value_token = ivrle_level_parser_read_value,
               .fp_next_level = ivrle_level_parser_next_level,
               .fp_next_event = ivrle_ds_reader_next_event}};
static struct level_parser_vtable const ivrle_item_vtable = {
    /* item reader interface */
    .reader = {.fp_key_token = ivrle_level_parser_read_key,
               .fp_value_token = ivrle_level_parser_read_value,
               .fp_next_level = ivrle_level_parser_next_level,
               .fp_next_event = ivrle_level_parser_next_event}};

struct level_parser
ivrle_level_parser_next_level(struct level_parser *level_parser,
                              const enum state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct level_parser new_item = {.vtable = &ivrle_item_vtable};
  return new_item;
}

struct level_parser get_new_ivrle_reader_ds() {
  struct level_parser new_item = {.vtable = &ivrle_ds_vtable};
  return new_item;
}

static struct level_emitter
ivrle_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state);

static struct level_emitter_vtable const g_ivrle_root_vtable = {
    /* ds writer interface */
    .level_emitter = {.fp_key_token = ivrle_level_emitter_write_key,
                      .fp_vl_token = ivrle_level_emitter_write_vl,
                      .fp_value_token = ivrle_level_emitter_write_value,
                      .fp_next_level = ivrle_level_emitter_next_level,
                      .fp_next_event = ivrle_ds_writer_next_event}};
static struct level_emitter_vtable const g_ivrle_item_vtable = {
    /* item writer interface */
    .level_emitter = {.fp_key_token = ivrle_level_emitter_write_key,
                      .fp_vl_token = ivrle_level_emitter_write_vl,
                      .fp_value_token = ivrle_level_emitter_write_value,
                      .fp_next_level = ivrle_level_emitter_next_level,
                      .fp_next_event = ivrle_level_emitter_next_event}};

struct level_emitter
ivrle_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct level_emitter new_item = {.vtable = &g_ivrle_item_vtable};
  return new_item;
}

void ivrle_init_level_emitter(struct level_emitter *new_item) {
  assert(new_item->da.tag == 0x0);
  new_item->vtable = &g_ivrle_root_vtable;
}
