#include "dicm_item.h"

#include "dicm_dst.h"
#include "dicm_src.h"

#include <assert.h>

bool dicm_vr_is_16(const dicm_vr_t vr) { return _is_vr16(vr); }

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

static inline bool _vl_is_valid(const dicm_vl_t vl) {
  return dicm_vl_is_undefined(vl) || vl % 2 == 0;
}

static inline bool _attribute_is_valid(const struct _attribute *da) {
  // 1. check triplet separately:
  const bool valid =
      _tag_is_valid(da->tag) && _vr_is_valid(da->vr) && _vl_is_valid(da->vl);
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

enum dicm_token _item_reader_next_impl(struct _item_reader *self,
                                       struct dicm_src *src) {
  union _ude ude;
  _Static_assert(16 == sizeof(ude), "16 bytes");
  _Static_assert(12 == sizeof(struct _ede32), "12 bytes");
  _Static_assert(8 == sizeof(struct _ede16), "8 bytes");
  _Static_assert(8 == sizeof(struct _ide), "8 bytes");
  int64_t ssize = dicm_src_read(src, ude.bytes, 8);
  if (ssize != 8) {
    assert(ssize >= 0);
    return ssize == 0 ? TOKEN_EOF : TOKEN_INVALID_DATA;
  }

  {
    const dicm_tag_t tag = _ide_get_tag(&ude);
    self->da.tag = tag;
    const dicm_vl_t ide_vl = _ide_get_vl(&ude);
    switch (tag) {
    case TAG_STARTITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      assert(_vl_is_valid(ide_vl));
      return _vl_is_valid(ide_vl) ? TOKEN_STARTITEM : TOKEN_INVALID_DATA;
    case TAG_ENDITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      assert(ide_vl == 0);
      return ide_vl == 0 ? TOKEN_ENDITEM : TOKEN_INVALID_DATA;
    case TAG_ENDSQITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      assert(ide_vl == 0);
      return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
    }
  }

  const dicm_vr_t vr = _ede16_get_vr(&ude);
  self->da.vr = vr;
  if (_is_vr16(vr)) {
    const dicm_vl_t vl = _ede16_get_vl(&ude);
    self->da.vl = vl;
  } else {
    // FIXME: this statement checks if VR is actually valid (0 padded), which is
    // redundant with `_attribute_is_valid`:
    if (ude.ede16.vl16 != 0)
      return TOKEN_INVALID_DATA;

    ssize = dicm_src_read(src, &ude.ede32.vl, 4);
    if (ssize != 4)
      return TOKEN_INVALID_DATA;

    const dicm_vl_t vl = _ede32_get_vl(&ude);
    self->da.vl = vl;
  }

  if (!_attribute_is_valid(&self->da)) {
    assert(0);
    return TOKEN_INVALID_DATA;
  }

  return TOKEN_KEY;
}

enum dicm_token _item_reader_next_impl3(struct _item_reader *self,
                                        struct dicm_src *src) {
  union _ude ude;
  _Static_assert(16 == sizeof(ude), "16 bytes");
  _Static_assert(12 == sizeof(struct _ede32), "12 bytes");
  _Static_assert(8 == sizeof(struct _ede16), "8 bytes");
  _Static_assert(8 == sizeof(struct _ide), "8 bytes");
  int64_t ssize = dicm_src_read(src, ude.bytes, 8);
  if (ssize != 8) {
    assert(ssize >= 0);
    return ssize == 0 ? TOKEN_EOF : TOKEN_INVALID_DATA;
  }

  {
    const dicm_tag_t tag = _ide_get_tag(&ude);
    self->da.tag = tag;
    const dicm_vl_t ide_vl = _ide_get_vl(&ude);
    self->da.vl = ide_vl;
    self->da.vr = VR_NONE;
    switch (tag) {
    case TAG_STARTITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      assert(_vl_is_valid(ide_vl));
      return _vl_is_valid(ide_vl) ? TOKEN_STARTITEM : TOKEN_INVALID_DATA;
    case TAG_ENDITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      assert(ide_vl == 0);
      return ide_vl == 0 ? TOKEN_ENDITEM : TOKEN_INVALID_DATA;
    case TAG_ENDSQITEM:
      self->da.vr = VR_NONE;
      self->da.vl = ide_vl;
      assert(ide_vl == 0);
      return ide_vl == 0 ? TOKEN_ENDSQITEM : TOKEN_INVALID_DATA;
    }
  }

#if 0
  // FIXME TODO
  if (!_attribute_is_valid(&self->da)) {
    assert(0);
    return TOKEN_INVALID_DATA;
  }
#endif

  return TOKEN_KEY;
}

static enum dicm_token _item_reader_next_impl2(struct _item_reader *self,
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

static enum dicm_token _item_reader_next_impl4(struct _item_reader *self,
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

enum dicm_state _ds_reader_next_event(struct _item_reader *self,
                                      const enum dicm_state current_state,
                                      struct dicm_src *src) {
  enum dicm_token next;
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSTREAM:
    next = item_reader_key_token(self, src);
    if (next == TOKEN_INVALID_DATA) {
      new_state = STATE_INVALID;
    } else if (next == TOKEN_EOF) {
      new_state = STATE_ENDSTREAM;
    } else {
      assert(next == TOKEN_KEY);
      new_state = STATE_STARTDOCUMENT;
    }
    break;
  case STATE_STARTDOCUMENT:
    next = TOKEN_KEY;
    new_state = next == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    break;
  case STATE_KEY:
    next = item_reader_value_token(self, src);
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

enum dicm_state _item_writer_write_key_explicit(struct _item_writer *self,
                                                struct dicm_dst *dst,
                                                const enum dicm_token token) {
  assert(token == TOKEN_KEY);
  union _ude ude;
  const bool is_vr16 = _ude_init(&ude, &self->da);
  const size_t len = is_vr16 ? 6u : 8u;
  int64_t dlen = dicm_dst_write(dst, &ude, len);
  assert(dlen == (int64_t)len);
  return STATE_KEY;
}

enum dicm_state _item_writer_write_key_implicit(struct _item_writer *self,
                                                struct dicm_dst *dst,
                                                const enum dicm_token token) {
  assert(token == TOKEN_KEY);
  union _ude ude;
  _ide_set_tag(&ude, self->da.tag);
  int64_t dlen = dicm_dst_write(dst, &ude.ide, 4);
  assert(dlen == 4);
  return STATE_KEY;
}

enum dicm_state _item_writer_write_vl_explicit(struct _item_writer *self,
                                               struct dicm_dst *dst,
                                               const enum dicm_token token) {
  assert(token >= 0);
  union _ude ude;
  const bool is_vr16 = _ude_init(&ude, &self->da);
  const size_t len16 = is_vr16 ? 2u : 4u;
  const uint32_t s = self->da.vl;
  int64_t dlen;
  if (is_vr16) {
    _ede16_set_vl(&ude, s);
    dlen = dicm_dst_write(dst, &ude.ede16.vl16, len16);
  } else {
    _ede32_set_vl(&ude, s);
    dlen = dicm_dst_write(dst, &ude.ede32.vl, len16);
  }
  assert(dlen == (int64_t)len16);

  return STATE_VALUE;
}

enum dicm_state _item_writer_write_vl_implicit(struct _item_writer *self,
                                               struct dicm_dst *dst,
                                               const enum dicm_token token) {
  assert(token >= 0);
  union _ude ude;
  const bool is_vr16 = false; //_ude_init(&ude, &self->da);
  const size_t len16 = is_vr16 ? 2u : 4u;
  const uint32_t s = self->da.vl;
  int64_t dlen;
  if (is_vr16) {
    _ede16_set_vl(&ude, s);
    dlen = dicm_dst_write(dst, &ude.ede16.vl16, len16);
  } else {
    _ede32_set_vl(&ude, s);
    dlen = dicm_dst_write(dst, &ude.ede32.vl, len16);
  }
  assert(dlen == (int64_t)len16);

  return STATE_VALUE;
}

/* valid for both explicit and implicit structures */
static enum dicm_state
_item_writer_write_sequence_markers(struct _item_writer *self,
                                    struct dicm_dst *dst,
                                    const enum dicm_token token) {
  assert(self);
  assert(token == TOKEN_STARTITEM || token == TOKEN_ENDSQITEM);
  enum dicm_state new_state = STATE_INVALID;
  union _ude ude;
  int64_t err;
  switch (token) {
  case TOKEN_STARTITEM:
    _ide_set_tag(&ude, TAG_STARTITEM);
    // FIXME: only undef sequence for now
    _ide_set_vl(&ude, VL_UNDEFINED);
    err = dicm_dst_write(dst, &ude.ide, 8);
    assert(err == 8);
    new_state = STATE_STARTITEM;
    break;
  case TOKEN_ENDSQITEM:
    _ide_set_tag(&ude, TAG_ENDSQITEM);
    _ide_set_vl(&ude, 0);
    err = dicm_dst_write(dst, &ude.ide, 8);
    assert(err == 8);
    new_state = STATE_ENDSEQUENCE;
    break;
  default:;
  }

  return new_state;
}

static enum dicm_state _item_writer_impl2(struct _item_writer *self,
                                          struct dicm_dst *dst,
                                          const enum dicm_token token) {
  assert(token == TOKEN_ENDITEM || token == TOKEN_KEY);
  enum dicm_state new_state = STATE_INVALID;
  union _ude ude;
  int64_t err;
  switch (token) {
  case TOKEN_KEY:
    new_state = item_writer_key_token(self, dst, token);
    assert(new_state == STATE_KEY);
    break;
  case TOKEN_ENDITEM:
    _ide_set_tag(&ude, TAG_ENDITEM);
    _ide_set_vl(&ude, 0);
    err = dicm_dst_write(dst, &ude.ide, 8);
    assert(err == 8);
    new_state = STATE_ENDITEM;
    break;
  default:;
  }

  return new_state;
}

enum dicm_state
_item_writer_write_value_encapsulated(struct _item_writer *self,
                                      struct dicm_dst *dst,
                                      const enum dicm_token token) {
  assert(token == TOKEN_VALUE || TOKEN_STARTSEQUENCE);
  enum dicm_state new_state = STATE_INVALID;
  switch (token) {
  case TOKEN_VALUE:
    new_state = STATE_VALUE;
    break;
  case TOKEN_STARTSEQUENCE:
    self->da.vl = VL_UNDEFINED;
    const bool enc = dicm_attribute_is_encapsulated_pixel_data(&self->da);
    {
      union _ude ude;
      _ede32_set_vl(&ude, VL_UNDEFINED);
      int64_t err = dicm_dst_write(dst, &ude.ede32.vl, 4);
      assert(err == 4);
    }
    new_state = enc ? STATE_STARTFRAGMENTS : STATE_STARTSEQUENCE;
    break;
  default:
    assert(0);
  }

  return new_state;
}

enum dicm_state _item_writer_write_value_common(struct _item_writer *self,
                                                struct dicm_dst *dst,
                                                const enum dicm_token token) {
  assert(token == TOKEN_VALUE || TOKEN_STARTSEQUENCE);
  enum dicm_state new_state = STATE_INVALID;
  switch (token) {
  case TOKEN_VALUE:
    new_state = STATE_VALUE;
    break;
  case TOKEN_STARTSEQUENCE:
    self->da.vl = VL_UNDEFINED;
    const bool enc =
        false; // dicm_attribute_is_encapsulated_pixel_data(&self->da);
    {
      union _ude ude;
      _ede32_set_vl(&ude, VL_UNDEFINED);
      int64_t err = dicm_dst_write(dst, &ude.ede32.vl, 4);
      assert(err == 4);
    }
    new_state = enc ? STATE_STARTFRAGMENTS : STATE_STARTSEQUENCE;
    break;
  default:
    assert(0);
  }

  return new_state;
}

enum dicm_state _ds_writer_next_event(struct _item_writer *self,
                                      const enum dicm_state current_state,
                                      struct dicm_dst *dst,
                                      const enum dicm_event_type next) {
  if (next == DICM_DOCUMENT_START_EVENT) {
    assert(current_state == STATE_STARTSTREAM);
    return STATE_STARTDOCUMENT;
  } else if (next == DICM_STREAM_END_EVENT) {
    assert(current_state == STATE_ENDDOCUMENT ||
           current_state == STATE_STARTSTREAM);
    return STATE_ENDSTREAM;
  }
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

enum dicm_state _item_reader_next_event(struct _item_reader *self,
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

enum dicm_state _item_writer_next_event(struct _item_writer *self,
                                        const enum dicm_state current_state,
                                        struct dicm_dst *dst,
                                        const enum dicm_event_type next) {
  const enum dicm_token token = event2token(next);
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_STARTSEQUENCE:
  case STATE_ENDITEM:
    new_state = _item_writer_write_sequence_markers(self, dst, token);
    break;
  case STATE_KEY:
    new_state = item_writer_value_token(self, dst, token);
    break;
  case STATE_VALUE:
  case STATE_STARTITEM:
  case STATE_ENDSEQUENCE:
    new_state = _item_writer_impl2(self, dst, token);
    break;
  default:
    assert(0);
  }
  return new_state;
}

enum dicm_token _fragments_reader_next_impl2(struct _item_reader *self,
                                             struct dicm_src *src) {
  assert(src);
  const dicm_vr_t vr = self->da.vr;
  assert(vr == VR_NONE);
  return TOKEN_VALUE;
}

enum dicm_token _fragments_reader_next_impl(struct _item_reader *self,
                                            struct dicm_src *src) {
  const enum dicm_token next = _item_reader_next_impl(self, src);
  return next == TOKEN_STARTITEM ? TOKEN_FRAGMENT : next;
}

/*
 * Fragment reader enter in state:
 * - STATE_STARTFRAGMENTS,
 * and exit in state:
 * - STATE_ENDSEQUENCE
 */
enum dicm_state
_fragments_reader_next_event(struct _item_reader *self,
                             const enum dicm_state current_state,
                             struct dicm_src *src) {
  enum dicm_token next;
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_VALUE:
  case STATE_STARTFRAGMENTS:
    next = item_reader_key_token(self, src);
    // FIXME: technically TOKEN_ENDSQITEM is impossible right here, this would
    // mean a duplicate Pixel Data was sent in the dataset.
    assert(next == TOKEN_FRAGMENT || next == TOKEN_ENDSQITEM);
    new_state =
        next == TOKEN_FRAGMENT
            ? STATE_FRAGMENT
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_FRAGMENT:
    next = item_reader_value_token(self, src);
    assert(next == TOKEN_VALUE);
    new_state = next == TOKEN_VALUE ? STATE_VALUE : STATE_INVALID;
    break;
  default:
    assert(0);
  }
  return new_state;
}

enum dicm_state _fragments_writer_key_token(struct _item_writer *self,
                                            struct dicm_dst *dst,
                                            const enum dicm_token token) {
  assert(self);
  assert(token >= 0);
  enum dicm_state ret = STATE_INVALID;
  union _ude ude;
  int64_t err;
  switch (token) {
  case TOKEN_FRAGMENT:
    _ide_set_tag(&ude, TAG_STARTITEM);
    err = dicm_dst_write(dst, &ude.ide, 4);
    assert(err == 4);
    ret = STATE_FRAGMENT;
    break;
  case TOKEN_ENDSQITEM:
    _ide_set_tag(&ude, TAG_ENDSQITEM);
    _ide_set_vl(&ude, 0);
    err = dicm_dst_write(dst, &ude.ide, 8);
    assert(err == 8);
    ret = STATE_ENDSEQUENCE;
    break;
  default:
    assert(0);
  }
  return ret;
}

enum dicm_state _fragments_writer_next_event(
    struct _item_writer *self, const enum dicm_state current_state,
    struct dicm_dst *dst, const enum dicm_event_type next) {
  const enum dicm_token token = event2token(next);
  enum dicm_state new_state = STATE_INVALID;
  switch (current_state) {
  case STATE_VALUE:
  case STATE_STARTFRAGMENTS:
    new_state = item_writer_key_token(self, dst, token);
    break;
  case STATE_FRAGMENT:
    assert(token == TOKEN_VALUE);
    new_state = STATE_VALUE;
    break;
  default:
    assert(0);
  }
  return new_state;
}

static struct _item_reader
se_item_reader_next_level(struct _item_reader *item_reader,
                          const enum dicm_state current_state);
static struct _item_reader
si_item_reader_next_level(struct _item_reader *item_reader,
                          const enum dicm_state current_state);

static struct _item_reader_vtable const se_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_key_token = _item_reader_next_impl,
               .fp_value_token = _item_reader_next_impl2,
               .fp_next_level = se_item_reader_next_level,
               .fp_next_event = _ds_reader_next_event}};
static struct _item_reader_vtable const si_ds_vtable = {
    /* ds reader interface */
    .reader = {.fp_key_token = _item_reader_next_impl3,
               .fp_value_token = _item_reader_next_impl4,
               .fp_next_level = si_item_reader_next_level,
               .fp_next_event = _ds_reader_next_event}};
static struct _item_reader_vtable const se_item_vtable = {
    /* item reader interface */
    .reader = {.fp_key_token = _item_reader_next_impl,
               .fp_value_token = _item_reader_next_impl2,
               .fp_next_level = se_item_reader_next_level,
               .fp_next_event = _item_reader_next_event}};
static struct _item_reader_vtable const si_item_vtable = {
    /* item reader interface */
    .reader = {.fp_key_token = _item_reader_next_impl3,
               .fp_value_token = _item_reader_next_impl4,
               .fp_next_level = si_item_reader_next_level,
               .fp_next_event = _item_reader_next_event}};
static struct _item_reader_vtable const se_fragment_vtable = {
    /* fragment reader interface */
    .reader = {.fp_key_token = _fragments_reader_next_impl,
               .fp_value_token = _fragments_reader_next_impl2,
               /*.fp_next_level = NULL, */
               .fp_next_event = _fragments_reader_next_event}};

struct _item_reader
se_item_reader_next_level(struct _item_reader *item_reader,
                          const enum dicm_state current_state) {
  struct _item_reader new_item = {};
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    new_item.vtable = &se_item_vtable;
    break;
  case STATE_STARTFRAGMENTS:
    new_item.vtable = &se_fragment_vtable;
    break;
  default:
    assert(0);
  }

  return new_item;
}

struct _item_reader
si_item_reader_next_level(struct _item_reader *item_reader,
                          const enum dicm_state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct _item_reader new_item = {.vtable = &si_item_vtable};
  return new_item;
}

#if 1
struct _item_reader get_new_reader_ds() {
  struct _item_reader new_item = {.vtable = &se_ds_vtable};
  return new_item;
}

struct _item_reader get_new_implicit_reader_ds() {
  struct _item_reader new_item = {.vtable = &si_ds_vtable};
  return new_item;
}

struct _item_reader get_new_reader_item() {
  struct _item_reader new_item = {.vtable = &se_item_vtable};
  return new_item;
}

struct _item_reader get_new_reader_frag() {
  struct _item_reader new_item = {.vtable = &se_fragment_vtable};
  return new_item;
}
#endif

static struct _item_writer
_item_next_level(struct _item_writer *self,
                 const enum dicm_state current_state);
static struct _item_writer
_item_next_level_implicit(struct _item_writer *self,
                          const enum dicm_state current_state);

static struct _item_writer_vtable const g_ds_vtable = {
    /* ds writer interface */
    .writer = {.fp_key_token = _item_writer_write_key_explicit,
               .fp_vl_token = _item_writer_write_vl_explicit,
               .fp_value_token = _item_writer_write_value_encapsulated,
               .fp_next_level = _item_next_level,
               .fp_next_event = _ds_writer_next_event}};
static struct _item_writer_vtable const g_ds_imp_vtable = {
    /* ds writer interface */
    .writer = {.fp_key_token = _item_writer_write_key_implicit,
               .fp_vl_token = _item_writer_write_vl_implicit,
               .fp_value_token = _item_writer_write_value_common,
               .fp_next_level = _item_next_level_implicit,
               .fp_next_event = _ds_writer_next_event}};
static struct _item_writer_vtable const g_item_vtable = {
    /* item writer interface */
    .writer = {.fp_key_token = _item_writer_write_key_explicit,
               .fp_vl_token = _item_writer_write_vl_explicit,
               .fp_value_token = _item_writer_write_value_encapsulated,
               .fp_next_level = _item_next_level,
               .fp_next_event = _item_writer_next_event}};
static struct _item_writer_vtable const g_item_vtable_si = {
    /* item writer interface */
    .writer = {.fp_key_token = _item_writer_write_key_implicit,
               .fp_vl_token = _item_writer_write_vl_implicit,
               .fp_value_token = _item_writer_write_value_common,
               .fp_next_level = _item_next_level_implicit,
               .fp_next_event = _item_writer_next_event}};
static struct _item_writer_vtable const g_fragment_vtable = {
    /* fragment writer interface */
    .writer = {.fp_key_token = _fragments_writer_key_token,
               .fp_vl_token = _item_writer_write_vl_explicit,
               .fp_value_token = _item_writer_write_value_encapsulated,
               /*.fp_next_level = NULL, */
               .fp_next_event = _fragments_writer_next_event}};

struct _item_writer _item_next_level(struct _item_writer *self,
                                     const enum dicm_state current_state) {
  struct _item_writer new_item = {};
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    new_item.vtable = &g_item_vtable;
    break;
  case STATE_STARTFRAGMENTS:
    new_item.vtable = &g_fragment_vtable;
    break;
  default:
    assert(0);
  }

  return new_item;
}

struct _item_writer
_item_next_level_implicit(struct _item_writer *self,
                          const enum dicm_state current_state) {
  assert(STATE_STARTSEQUENCE == current_state);
  struct _item_writer new_item = {.vtable = &g_item_vtable_si};
  return new_item;
}

void init_root_item_writer(struct _item_writer *new_item,
                           const enum dicm_structure_type structure_type) {
  assert(new_item->da.tag == 0x0);
  switch (structure_type) {
  case DICM_STRUCTURE_ENCAPSULATED:
    new_item->vtable = &g_ds_vtable;
    break;
  case DICM_STRUCTURE_IMPLICIT:
    new_item->vtable = &g_ds_imp_vtable;
    break;
  default:
    assert(0);
  }
}
