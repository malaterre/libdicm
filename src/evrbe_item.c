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

static inline bool _attribute_is_valid(const struct _attribute *da) {
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

#define item_reader_key_token(t, src)                                          \
  ((t)->vtable->reader.fp_key_token((t), (src)))
#define item_reader_value_token(t, src)                                        \
  ((t)->vtable->reader.fp_value_token((t), (src)))

#define item_writer_key_token(t, dst, tok)                                     \
  ((t)->vtable->writer.fp_key_token((t), (dst), (tok)))
#define item_writer_value_token(t, dst, tok)                                   \
  ((t)->vtable->writer.fp_value_token((t), (dst), (tok)))

static enum dicm_token evrbe_item_reader_read_key(struct _item_reader *self,
                                                  struct dicm_src *src) {
  struct dual dual;
  int64_t ssize = dicm_src_read(src, &dual.ivr, 8);
  if (ssize != 8) {
    return TOKEN_EOF;
  }

  const uint32_t tag = bswap_32(dual.ivr.tag);
  self->da.tag = tag;
  {
    const uint32_t ide_vl = bswap_32(dual.ivr.vl);
    switch (tag) {
    case TAG_STARTITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return vl_is_valid(ide_vl) ? TOKEN_STARTITEM : TOKEN_INVALID_DATA;
    case TAG_ENDITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDITEM : TOKEN_INVALID_DATA;
    case TAG_ENDSQITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
    }
  }

  const uint32_t vr = dual.evr.vr16;
  self->da.vr = vr;
  if (_is_vr16(vr)) {
    const uint32_t vl = bswap_16(dual.evr.vl16);
    self->da.vl = vl;
  } else {
    ssize = dicm_src_read(src, &dual.evr.vl32, 4);
    if (ssize != 4) {
      return TOKEN_EOF;
    }
    const uint32_t vl = bswap_32(dual.evr.vl32);
    self->da.vl = vl;
  }

  if (!_attribute_is_valid(&self->da)) {
    assert(0);
    return TOKEN_INVALID_DATA;
  }

  return TOKEN_KEY;
}

static enum dicm_token evrbe_item_reader_read_value(struct _item_reader *self,
                                                    struct dicm_src *src) {
  assert(src);
  const dicm_vr_t vr = self->da.vr;
  if (vr == VR_SQ) {
    assert(dicm_vl_is_undefined(self->da.vl)); // for now
    return TOKEN_STARTSEQUENCE;
  } else {
    assert(!dicm_vl_is_undefined(self->da.vl));
    return TOKEN_VALUE;
  }
}

static enum dicm_state
evrbe_ds_reader_next_event(struct _item_reader *self,
                           const enum dicm_state current_state,
                           struct dicm_src *src) {
  enum dicm_token next;
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTDOCUMENT:
    next = item_reader_key_token(self, src);
    /* empty document is an error */
    new_state = next == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    break;
  case STATE_KEY:
    next = item_reader_value_token(self, src);
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE);
    new_state = next == TOKEN_VALUE
                    ? STATE_VALUE
                    : (next == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                   : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE: // re-enter case
    next = item_reader_key_token(self, src);
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

static enum dicm_state
evrbe_item_reader_next_event(struct _item_reader *self,
                             const enum dicm_state current_state,
                             struct dicm_src *src) {
  enum dicm_token next;
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE: // default enter
  case STATE_ENDITEM:
    next = item_reader_key_token(self, src);
    assert(next == TOKEN_STARTITEM || next == TOKEN_ENDSQITEM);
    new_state =
        next == TOKEN_STARTITEM
            ? STATE_STARTITEM
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_KEY:
    next = item_reader_value_token(self, src);
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE);
    new_state = next == TOKEN_VALUE
                    ? STATE_VALUE
                    : (next == TOKEN_STARTSEQUENCE ? STATE_STARTSEQUENCE
                                                   : STATE_INVALID);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE: // re-enter case
  case STATE_STARTITEM:
    next = item_reader_key_token(self, src);
    assert(next == TOKEN_KEY || next == TOKEN_ENDITEM);
    new_state = next == TOKEN_KEY
                    ? STATE_KEY
                    : (next == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  default:
    assert(0);
  }
  return new_state;
}

static enum dicm_state
evrbe_item_writer_write_key(struct _item_writer *self, struct dicm_dst *dst,
                            const enum dicm_token token) {
  assert(token == TOKEN_KEY);
  const struct evr evr = _evr_init2(&self->da);
  const size_t key_len = evr_get_key_size(evr);
  const int64_t dlen = dicm_dst_write(dst, evr.bytes, key_len);
  return dlen == (int64_t)key_len ? STATE_KEY : STATE_INVALID;
}

static enum dicm_state evrbe_item_writer_write_vl(struct _item_writer *self,
                                                  struct dicm_dst *dst,
                                                  const enum dicm_token token) {
  assert(token >= 0);
  const struct evr evr = _evr_init2(&self->da);
  const size_t vl_len = evr.vr_size;
  const int64_t dlen = dicm_dst_write(dst, &evr.vl, vl_len);
  return dlen == (int64_t)vl_len ? STATE_VALUE : STATE_INVALID;
}

#define BSWAP32(x)                                                             \
  ((((x)&0x000000ff) << 24) | (((x)&0x0000ff00) << 8) |                        \
   (((x)&0x00ff0000) >> 8) | (((x)&0xff000000) >> 24))

static const struct ivr evrbe_start_item = {.tag = BSWAP32(TAG_STARTITEM),
                                            .vl = VL_UNDEFINED};
static const struct ivr evrbe_end_item = {.tag = BSWAP32(TAG_ENDITEM), .vl = 0};
static const struct ivr evrbe_end_sq_item = {.tag = BSWAP32(TAG_ENDSQITEM),
                                             .vl = 0};

static enum dicm_state
evrbe_item_writer_write_startitem_endsqitem(struct _item_writer *self,
                                            struct dicm_dst *dst,
                                            const enum dicm_token token) {
  assert(self);
  enum dicm_state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_STARTITEM:
    // FIXME: only undef sequence for now
    dlen = dicm_dst_write(dst, evrbe_start_item.bytes, 8);
    new_state = dlen == 8 ? STATE_STARTITEM : STATE_INVALID;
    break;
  case TOKEN_ENDSQITEM:
    dlen = dicm_dst_write(dst, evrbe_end_sq_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDSEQUENCE : STATE_INVALID;
    break;
  default:;
  }

  return new_state;
}

static enum dicm_state
evrbe_item_writer_write_key_enditem(struct _item_writer *self,
                                    struct dicm_dst *dst,
                                    const enum dicm_token token) {
  assert(token == TOKEN_ENDITEM || token == TOKEN_KEY);
  enum dicm_state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_KEY:
    /* return STATE_INVALID in case of write error */
    new_state = item_writer_key_token(self, dst, token);
    break;
  case TOKEN_ENDITEM:
    /* FIXME this is identical to case above */
    dlen = dicm_dst_write(dst, evrbe_end_item.bytes, 8);
    new_state = dlen == 8 ? STATE_ENDITEM : STATE_INVALID;
    break;
  default:;
  }

  return new_state;
}

static enum dicm_state
evrbe_item_writer_write_value(struct _item_writer *self, struct dicm_dst *dst,
                              const enum dicm_token token) {
  assert(token == TOKEN_VALUE || TOKEN_STARTSEQUENCE);
  enum dicm_state new_state = STATE_INVALID;
  int64_t dlen;
  switch (token) {
  case TOKEN_VALUE:
    new_state = STATE_VALUE;
    break;
  case TOKEN_STARTSEQUENCE:
    self->da.vl = VL_UNDEFINED;
    dlen = dicm_dst_write(dst, &evrbe_start_item.vl, 4);
    new_state = dlen == 4 ? STATE_STARTSEQUENCE : STATE_INVALID;
    break;
  default:;
  }

  return new_state;
}

static enum dicm_state evrbe_ds_writer_next_event(
    struct _item_writer *self, const enum dicm_state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  const enum dicm_token token = event2token(next);
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTDOCUMENT:
    new_state = item_writer_key_token(self, dst, token);
    assert(new_state == STATE_KEY);
    break;
  case STATE_KEY:
    new_state = item_writer_value_token(self, dst, token);
    break;
  case STATE_VALUE:
  case STATE_ENDSEQUENCE:
    if (token == TOKEN_KEY) {
      new_state = item_writer_key_token(self, dst, token);
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

static enum dicm_state evrbe_item_writer_next_event(
    struct _item_writer *self, const enum dicm_state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  const enum dicm_token token = event2token(next);
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE:
  case STATE_ENDITEM:
    new_state = evrbe_item_writer_write_startitem_endsqitem(self, dst, token);
    break;
  case STATE_KEY:
    new_state = item_writer_value_token(self, dst, token);
    break;
  case STATE_VALUE:
  case STATE_STARTITEM:
  case STATE_ENDSEQUENCE:
    new_state = evrbe_item_writer_write_key_enditem(self, dst, token);
    break;
  default:
    assert(0);
  }
  return new_state;
}

static struct _item_reader
evrbe_item_reader_next_level(struct _item_reader *item_reader,
                             const enum dicm_state current_state);

static struct _item_reader_vtable const evrbe_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_key_token = evrbe_item_reader_read_key,
               .fp_value_token = evrbe_item_reader_read_value,
               .fp_next_level = evrbe_item_reader_next_level,
               .fp_next_event = evrbe_ds_reader_next_event}};
static struct _item_reader_vtable const evrbe_item_vtable = {
    /* item reader interface */
    .reader = {.fp_key_token = evrbe_item_reader_read_key,
               .fp_value_token = evrbe_item_reader_read_value,
               .fp_next_level = evrbe_item_reader_next_level,
               .fp_next_event = evrbe_item_reader_next_event}};

struct _item_reader
evrbe_item_reader_next_level(struct _item_reader *item_reader,
                             const enum dicm_state current_state) {
  struct _item_reader new_item = {};
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    new_item.vtable = &evrbe_item_vtable;
    break;
  default:
    assert(0);
  }

  return new_item;
}

struct _item_reader get_new_evrbe_reader_ds() {
  struct _item_reader new_item = {.vtable = &evrbe_ds_vtable};
  return new_item;
}

static struct _item_writer
evrbe_item_next_level(struct _item_writer *self,
                      const enum dicm_state current_state);

static struct _item_writer_vtable const g_evrbe_root_vtable = {
    /* ds writer interface */
    .writer = {.fp_key_token = evrbe_item_writer_write_key,
               .fp_vl_token = evrbe_item_writer_write_vl,
               .fp_value_token = evrbe_item_writer_write_value,
               .fp_next_level = evrbe_item_next_level,
               .fp_next_event = evrbe_ds_writer_next_event}};
static struct _item_writer_vtable const g_evrbe_item_vtable = {
    /* item writer interface */
    .writer = {.fp_key_token = evrbe_item_writer_write_key,
               .fp_vl_token = evrbe_item_writer_write_vl,
               .fp_value_token = evrbe_item_writer_write_value,
               .fp_next_level = evrbe_item_next_level,
               .fp_next_event = evrbe_item_writer_next_event}};

struct _item_writer evrbe_item_next_level(struct _item_writer *self,
                                          const enum dicm_state current_state) {
  struct _item_writer new_item = {};
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    new_item.vtable = &g_evrbe_item_vtable;
    break;
  default:
    assert(0);
  }

  return new_item;
}

void evrbe_init_item_writer(struct _item_writer *new_item) {
  assert(new_item->da.tag == 0x0);
  new_item->vtable = &g_evrbe_root_vtable;
}
