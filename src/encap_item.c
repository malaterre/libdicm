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
  ((t)->vtable->reader.fp_key_token((t), (src)))
#define level_parser_value_token(t, src)                                       \
  ((t)->vtable->reader.fp_value_token((t), (src)))

#define level_emitter_key_token(t, dst, tok)                                   \
  ((t)->vtable->level_emitter.fp_key_token((t), (dst), (tok)))
#define level_emitter_value_token(t, dst, tok)                                 \
  ((t)->vtable->level_emitter.fp_value_token((t), (dst), (tok)))

#define SWAP_TAG(x) ((((x)&0x0000ffff) << 16u) | (((x)&0xffff0000) >> 16u))

static inline uint32_t evrle2tag(const uint32_t tag_bytes) {
  return SWAP_TAG(tag_bytes);
}

enum EVRLE_SPECIAL_TAGS {
  EVRLE_TAG_STARTITEM = SWAP_TAG(TAG_STARTITEM),
  EVRLE_TAG_ENDITEM = SWAP_TAG(TAG_ENDITEM),
  EVRLE_TAG_ENDSQITEM = SWAP_TAG(TAG_ENDSQITEM),
};

static const struct ivr evrle_start_item = {.tag = EVRLE_TAG_STARTITEM,
                                            .vl = VL_UNDEFINED};
static const struct ivr evrle_end_item = {.tag = EVRLE_TAG_ENDITEM, .vl = 0};
static const struct ivr evrle_end_sq_item = {.tag = EVRLE_TAG_ENDSQITEM,
                                             .vl = 0};

static enum token encap_level_parser_read_key(struct level_parser *self,
                                              struct dicm_src *src) {
  struct dual dual;
  int64_t ssize = dicm_src_read(src, &dual.ivr, 8);
  if (ssize != 8) {
    /* EOF is not an error at root level */
    return ssize == 0 ? TOKEN_EOF : TOKEN_INVALID_DATA;
  }

  const uint32_t tag = evrle2tag(dual.ivr.tag);
  self->da.tag = tag;
  {
    const uint32_t ide_vl = dual.ivr.vl;
    switch (tag) {
    case TAG_STARTITEM:
      /* FIXME: no bswap needed at this point */
      assert(dual.ivr.tag == EVRLE_TAG_STARTITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return vl_is_valid(ide_vl) ? TOKEN_STARTITEM : TOKEN_INVALID_DATA;
    case TAG_ENDITEM:
      assert(dual.ivr.tag == EVRLE_TAG_ENDITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDITEM : TOKEN_INVALID_DATA;
    case TAG_ENDSQITEM:
      assert(dual.ivr.tag == EVRLE_TAG_ENDSQITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
    }
  }

  const uint32_t vr = dual.evr.vr16;
  self->da.vr = vr;
  if (_is_vr16(vr)) {
    const uint32_t vl = dual.evr.vl16;
    self->da.vl = vl;
  } else {
    ssize = dicm_src_read(src, &dual.evr.vl32, 4);
    if (ssize != 4) {
      return TOKEN_INVALID_DATA;
    }
    const uint32_t vl = dual.evr.vl32;
    self->da.vl = vl;
  }

  if (!_attribute_is_valid(&self->da)) {
    assert(0);
    return TOKEN_INVALID_DATA;
  }

  return TOKEN_KEY;
}

static enum token encap_level_parser_read_value(struct level_parser *self,
                                                struct dicm_src *src) {
  assert(src);
  const dicm_vr_t vr = self->da.vr;
  if (dicm_attribute_is_encapsulated_pixel_data(&self->da)) {
    return TOKEN_STARTFRAGMENTS;
  } else if (vr == VR_SQ) {
    assert(dicm_vl_is_undefined(self->da.vl)); // for now
    return TOKEN_STARTSEQUENCE;
  } else {
    assert(!dicm_vl_is_undefined(self->da.vl));
    return TOKEN_VALUE;
  }
}

static enum state encap_root_reader_next_event(struct level_parser *self,
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
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE ||
           next == TOKEN_STARTFRAGMENTS);
    new_state =
        next == TOKEN_VALUE
            ? STATE_VALUE
            : (next == TOKEN_STARTSEQUENCE
                   ? STATE_STARTSEQUENCE
                   : (next == TOKEN_STARTFRAGMENTS ? STATE_STARTFRAGMENTS
                                                   : STATE_INVALID));
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE:
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

static enum state encap_level_parser_next_event(struct level_parser *self,
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
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE ||
           next == TOKEN_STARTFRAGMENTS);
    new_state =
        next == TOKEN_VALUE
            ? STATE_VALUE
            : (next == TOKEN_STARTSEQUENCE
                   ? STATE_STARTSEQUENCE
                   : (next == TOKEN_STARTFRAGMENTS ? STATE_STARTFRAGMENTS
                                                   : STATE_INVALID));
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

static enum state encap_level_emitter_write_key(struct level_emitter *self,
                                                struct dicm_dst *dst,
                                                const enum token token) {
  enum state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_KEY: {
    const struct evr evr = _evr_init1(&self->da);
    const size_t key_size = evr_get_key_size(evr);
    dlen = dicm_dst_write(dst, evr.bytes, key_size);
    new_state = dlen == (int64_t)key_size ? STATE_KEY : STATE_INVALID;
  } break;
  case TOKEN_STARTITEM:
    /* FIXME: only undef sequence for now */
    dlen = dicm_dst_write(dst, evrle_start_item.bytes, 8);
    new_state = dlen == 8 ? STATE_STARTITEM : STATE_INVALID;
    break;
  case TOKEN_ENDITEM:
    dlen = dicm_dst_write(dst, evrle_end_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDITEM : STATE_INVALID;
    break;
  case TOKEN_ENDSQITEM:
    dlen = dicm_dst_write(dst, evrle_end_sq_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDSEQUENCE : STATE_INVALID;
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum state encap_level_emitter_write_vl(struct level_emitter *self,
                                               struct dicm_dst *dst,
                                               const enum token token) {
  /* FIXME one step key + vl */
  assert(token == TOKEN_VALUE);
  const struct evr evr = _evr_init1(&self->da);
  const size_t vl_len = evr.vr_size;
  const int64_t dlen = dicm_dst_write(dst, &evr.vl, vl_len);
  return dlen == (int64_t)vl_len ? STATE_VALUE : STATE_INVALID;
}

static enum state encap_level_emitter_write_value(struct level_emitter *self,
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
    const bool enc = dicm_attribute_is_encapsulated_pixel_data(&self->da);
    /* write to dest */
    dlen = dicm_dst_write(dst, &evrle_start_item.vl, 4);
    new_state = dlen == 4 ? (enc ? STATE_STARTFRAGMENTS : STATE_STARTSEQUENCE)
                          : STATE_INVALID;
    break;
  default:;
  }

  return new_state;
}

static enum state encap_root_writer_next_event(
    struct level_emitter *self, const enum state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
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
    if (token == TOKEN_EOF) {
      new_state = STATE_ENDDOCUMENT;
    } else {
      new_state = level_emitter_key_token(self, dst, token);
      assert(new_state == STATE_KEY);
    }
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum state encap_level_emitter_next_event(
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
  default:;
  }
  return new_state;
}

static enum token encap_frag_reader_read_value(struct level_parser *self,
                                               struct dicm_src *src) {
  assert(src);
  const dicm_vr_t vr = self->da.vr;
  assert(vr == VR_NONE);
  return TOKEN_VALUE;
}

static enum token encap_frag_reader_read_key(struct level_parser *self,
                                             struct dicm_src *src) {
  struct dual dual;
  int64_t ssize = dicm_src_read(src, &dual.ivr, 8);
  if (ssize != 8) {
    return TOKEN_INVALID_DATA;
  }

  const uint32_t tag = evrle2tag(dual.ivr.tag);
  self->da.tag = tag;
  {
    const uint32_t ide_vl = dual.ivr.vl;
    switch (tag) {
    case TAG_STARTITEM:
      /* FIXME: no bswap needed at this point */
      assert(dual.ivr.tag == EVRLE_TAG_STARTITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return vl_is_valid(ide_vl) ? TOKEN_FRAGMENT : TOKEN_INVALID_DATA;
    case TAG_ENDSQITEM:
      assert(dual.ivr.tag == EVRLE_TAG_ENDSQITEM);
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
    }
  }
  return TOKEN_INVALID_DATA;
}

/*
 * Fragment reader enter in state:
 * - STATE_STARTFRAGMENTS,
 * and exit in state:
 * - STATE_ENDSEQUENCE
 */
static enum state encap_frag_reader_next_event(struct level_parser *self,
                                               const enum state current_state,
                                               struct dicm_src *src) {
  enum token next;
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_VALUE:
  case STATE_STARTFRAGMENTS:
    next = level_parser_key_token(self, src);
    // FIXME: technically TOKEN_ENDSQITEM is impossible right here, this would
    // mean a duplicate Pixel Data was sent in the dataset.
    assert(next == TOKEN_FRAGMENT || next == TOKEN_ENDSQITEM);
    new_state =
        next == TOKEN_FRAGMENT
            ? STATE_FRAGMENT
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_FRAGMENT:
    next = level_parser_value_token(self, src);
    assert(next == TOKEN_VALUE);
    new_state = next == TOKEN_VALUE ? STATE_VALUE : STATE_INVALID;
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum state encap_frag_writer_write_key(struct level_emitter *self,
                                              struct dicm_dst *dst,
                                              const enum token token) {
  enum state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_FRAGMENT:
    dlen = dicm_dst_write(dst, evrle_start_item.bytes, 4);
    new_state = dlen == 4 ? STATE_FRAGMENT : STATE_INVALID;
    break;
  case TOKEN_ENDSQITEM:
    dlen = dicm_dst_write(dst, evrle_end_sq_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDSEQUENCE : STATE_INVALID;
    break;
  default:;
  }
  return new_state;
}

static enum state encap_frag_writer_write_value(struct level_emitter *self,
                                                struct dicm_dst *dst,
                                                const enum token token) {
  assert(token == TOKEN_VALUE);
  enum state new_state = STATE_INVALID;
  switch (token) {
  case TOKEN_VALUE:
    new_state = STATE_VALUE;
    break;
  default:;
  }

  return new_state;
}

static enum state encap_frag_writer_next_event(
    struct level_emitter *self, const enum state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  const enum token token = event2token(next);
  enum state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_VALUE:
  case STATE_STARTFRAGMENTS:
    new_state = level_emitter_key_token(self, dst, token);
    break;
  case STATE_FRAGMENT:
    new_state = level_emitter_value_token(self, dst, token);
    break;
  default:;
  }
  return new_state;
}

static struct level_parser
encap_level_parser_next_level(struct level_parser *level_parser,
                              const enum state current_state);

static struct level_parser_vtable const encap_root_vtable = {
    /* ds reader interface */
    .reader = {.fp_key_token = encap_level_parser_read_key,
               .fp_value_token = encap_level_parser_read_value,
               .fp_next_level = encap_level_parser_next_level,
               .fp_next_event = encap_root_reader_next_event}};
static struct level_parser_vtable const encap_item_vtable = {
    /* item reader interface */
    .reader = {.fp_key_token = encap_level_parser_read_key,
               .fp_value_token = encap_level_parser_read_value,
               .fp_next_level = encap_level_parser_next_level,
               .fp_next_event = encap_level_parser_next_event}};
static struct level_parser_vtable const encap_frag_vtable = {
    /* fragment reader interface */
    .reader = {.fp_key_token = encap_frag_reader_read_key,
               .fp_value_token = encap_frag_reader_read_value,
               .fp_next_level = NULL, /* no nested fragment */
               .fp_next_event = encap_frag_reader_next_event}};

struct level_parser
encap_level_parser_next_level(struct level_parser *level_parser,
                              const enum state current_state) {
  struct level_parser new_item = {.da = 0};
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    new_item.vtable = &encap_item_vtable;
    break;
  case STATE_STARTFRAGMENTS:
    new_item.vtable = &encap_frag_vtable;
    break;
  default:;
  }

  return new_item;
}

struct level_parser get_new_reader_ds() {
  struct level_parser new_item = {.vtable = &encap_root_vtable};
  return new_item;
}

struct level_parser get_new_reader_item() {
  struct level_parser new_item = {.vtable = &encap_item_vtable};
  return new_item;
}

struct level_parser get_new_reader_frag() {
  struct level_parser new_item = {.vtable = &encap_frag_vtable};
  return new_item;
}

static struct level_emitter
encap_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state);

static struct level_emitter_vtable const g_encap_root_vtable = {
    /* ds writer interface */
    .level_emitter = {.fp_key_token = encap_level_emitter_write_key,
                      .fp_vl_token = encap_level_emitter_write_vl,
                      .fp_value_token = encap_level_emitter_write_value,
                      .fp_next_level = encap_level_emitter_next_level,
                      .fp_next_event = encap_root_writer_next_event}};
static struct level_emitter_vtable const g_encap_item_vtable = {
    /* item writer interface */
    .level_emitter = {.fp_key_token = encap_level_emitter_write_key,
                      .fp_vl_token = encap_level_emitter_write_vl,
                      .fp_value_token = encap_level_emitter_write_value,
                      .fp_next_level = encap_level_emitter_next_level,
                      .fp_next_event = encap_level_emitter_next_event}};
static struct level_emitter_vtable const g_encap_frag_vtable = {
    /* fragment writer interface */
    .level_emitter = {.fp_key_token = encap_frag_writer_write_key,
                      .fp_vl_token = encap_level_emitter_write_vl,
                      .fp_value_token = encap_frag_writer_write_value,
                      .fp_next_level = NULL, /* no nested fragment */
                      .fp_next_event = encap_frag_writer_next_event}};

struct level_emitter
encap_level_emitter_next_level(struct level_emitter *self,
                               const enum state current_state) {
  struct level_emitter new_item = {.da = 0};
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    new_item.vtable = &g_encap_item_vtable;
    break;
  case STATE_STARTFRAGMENTS:
    new_item.vtable = &g_encap_frag_vtable;
    break;
  default:;
  }

  return new_item;
}

void encap_init_level_emitter(struct level_emitter *new_item) {
  assert(new_item->da.tag == 0x0);
  new_item->vtable = &g_encap_root_vtable;
}
