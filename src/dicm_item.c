#include "dicm_item.h"

#include "dicm_dst.h"
#include "dicm_src.h"

#include <assert.h>

bool dicm_vr_is_16(const dicm_vr_t vr) { return _is_vr16(vr); }

static inline bool _tag_is_valid(const dicm_tag_t tag) {
  // The following cases have been handled by design:
  assert(tag != TAG_STARTITEM && tag != TAG_ENDITEM && tag != TAG_ENDSQITEM);
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

static enum dicm_token _item_reader_next_impl(struct _item_reader *self,
                                              struct dicm_src *src) {
  union _ude ude;
  _Static_assert(16 == sizeof(ude), "16 bytes");
  _Static_assert(12 == sizeof(struct _ede32), "12 bytes");
  _Static_assert(8 == sizeof(struct _ede16), "8 bytes");
  _Static_assert(8 == sizeof(struct _ide), "8 bytes");
  int ssize = dicm_src_read(src, ude.bytes, 8);
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

static inline enum dicm_token
_item_reader_next_impl2(struct _item_reader *self) {
  const dicm_vr_t vr = self->da.vr;
  if (dicm_attribute_is_encapsulated_pixel_data(&self->da)) {
    return TOKEN_STARTFRAGMENTS;
  } else if (vr == VR_SQ) {
    return TOKEN_STARTSEQUENCE;
  } else {
    assert(!dicm_vl_is_undefined(self->da.vl));
    self->value_length_pos = 0;

    return TOKEN_VALUE;
  }
}

enum dicm_event_type _ds_reader_next_event(struct _item_reader *self,
                                           struct dicm_src *src) {
  const enum dicm_state current_state = self->current_item_state;
  enum dicm_token next;
  switch (current_state) {
  case STATE_STARTDATASET:
    // see code in STATE_STARTSTREAM:
    next = TOKEN_KEY;
    assert(next == TOKEN_KEY);
    self->current_item_state = next == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    break;
  case STATE_KEY:
    next = _item_reader_next_impl2(self);
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE ||
           next == TOKEN_STARTFRAGMENTS);
    self->current_item_state =
        next == TOKEN_VALUE
            ? STATE_VALUE
            : (next == TOKEN_STARTSEQUENCE
                   ? STATE_STARTSEQUENCE
                   : (next == TOKEN_STARTFRAGMENTS ? STATE_STARTFRAGMENTS
                                                   : STATE_INVALID));
    break;
  case STATE_ENDSEQUENCE: // re-enter case
  case STATE_VALUE:
    // TODO: check user has consumed everything
    assert(dicm_vl_is_undefined(self->da.vl) ||
           self->da.vl == self->value_length_pos);
    next = _item_reader_next_impl(self, src);
    if (next == TOKEN_EOF) {
      self->current_item_state = STATE_ENDDATASET;
    } else {
      assert(next == TOKEN_KEY);
      self->current_item_state = next == TOKEN_KEY ? STATE_KEY : STATE_INVALID;
    }
    break;
  case STATE_ENDDATASET: // exit state
    self->current_item_state = STATE_ENDSTREAM;
    return DICM_STREAM_END_EVENT;
  case STATE_STARTSTREAM: // default start-up
    // We are reading a full attribute info here, and
    next = _item_reader_next_impl(self, src);
    assert(next == TOKEN_KEY || next == TOKEN_EOF);
    if (next == TOKEN_EOF) {
      return DICM_STREAM_END_EVENT;
    }
    // else
    self->current_item_state = STATE_STARTDATASET;
    return DICM_DATASET_START_EVENT;
  default:
    assert(0);
  }
  const enum dicm_event_type event_next = token2event(next);
  return event_next;
}

static int _item_writer_next_token_impl(struct _item_writer *self,
                                        struct dicm_dst *dst,
                                        const enum dicm_token token) {
  assert(token == TOKEN_KEY);
  union _ude ude;
  const bool is_vr16 = _ude_init(&ude, &self->da);
  const size_t len = is_vr16 ? 6u : 8u;
  int64_t dlen = dicm_dst_write(dst, &ude, len);
  assert(dlen == (int64_t)len);
  return 0;
}

int _ds_writer_next_token(struct _item_writer *self, struct dicm_dst *dst,
                          const enum dicm_event_type next) {
  const enum dicm_state current_state = self->current_item_state;
  const enum dicm_token token = event2token(next);
  int ret = -1;
  switch (current_state) {
  case STATE_STARTDATASET:
    assert(token == TOKEN_KEY);
    assert(self->value_length_pos == VL_UNDEFINED);
    ret = _item_writer_next_token_impl(self, dst, token);
    assert(ret == 0);
    self->current_item_state = STATE_KEY;
    ret = 0;
    break;
  case STATE_KEY:
    if (token == TOKEN_VALUE) {
      assert(self->value_length_pos != VL_UNDEFINED);
      assert(self->value_length_pos <= self->da.vl);
      if (self->value_length_pos == self->da.vl)
        self->current_item_state = STATE_VALUE;
      else {
        assert(0);
      }
      ret = 0;
    } else {
      assert(token == TOKEN_STARTSEQUENCE);
      self->da.vl = VL_UNDEFINED;
      const bool enc = dicm_attribute_is_encapsulated_pixel_data(&self->da);
      {
        union _ude ude;
        _ede32_set_vl(&ude, VL_UNDEFINED);
        int err = dicm_dst_write(dst, &ude.ede32.vl, 4);
        assert(err == 4);
      }
      self->current_item_state =
          enc ? STATE_STARTFRAGMENTS : STATE_STARTSEQUENCE;
      ret = 0;
    }
    break;
  case STATE_VALUE:
    if (token == TOKEN_KEY) {
      ret = _item_writer_next_token_impl(self, dst, token);
      assert(ret == 0);
      self->current_item_state = STATE_KEY;
    } else {
      assert(token == TOKEN_EOF);
      self->current_item_state = STATE_ENDDATASET;
      ret = 0;
    }
    break;
  case STATE_ENDSEQUENCE:
    if (token == TOKEN_KEY) {
      ret = _item_writer_next_token_impl(self, dst, token);
      assert(ret == 0);
      self->current_item_state = STATE_KEY;
      ret = 0;
    } else {
      assert(token == TOKEN_EOF);
      self->current_item_state = STATE_ENDDATASET;
      ret = 0;
    }
    break;
  default:
    assert(0);
    break;
  }
  return ret;
}

enum dicm_event_type _item_reader_next_event(struct _item_reader *self,
                                             struct dicm_src *src) {
  const enum dicm_state current_state = self->current_item_state;
  enum dicm_token next;
  switch (current_state) {
  case STATE_STARTSEQUENCE: // default enter
    next = _item_reader_next_impl(self, src);
    assert(next == TOKEN_STARTITEM || next == TOKEN_ENDSQITEM);
    self->current_item_state =
        next == TOKEN_STARTITEM
            ? STATE_STARTITEM
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_STARTITEM:
    next = _item_reader_next_impl(self, src);
    assert(next == TOKEN_KEY || next == TOKEN_ENDITEM);
    self->current_item_state =
        next == TOKEN_KEY
            ? STATE_KEY
            : (next == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  case STATE_KEY:
    next = _item_reader_next_impl2(self);
    assert(next == TOKEN_VALUE || next == TOKEN_STARTSEQUENCE ||
           next == TOKEN_STARTFRAGMENTS);
    self->current_item_state =
        next == TOKEN_VALUE
            ? STATE_VALUE
            : (next == TOKEN_STARTSEQUENCE
                   ? STATE_STARTSEQUENCE
                   : (next == TOKEN_STARTFRAGMENTS ? STATE_STARTFRAGMENTS
                                                   : STATE_INVALID));
    break;
  case STATE_VALUE:
    // TODO: check user has consumed everything
    assert(self->da.vl == self->value_length_pos);
    next = _item_reader_next_impl(self, src);
    assert(next == TOKEN_KEY || next == TOKEN_ENDITEM);
    self->current_item_state =
        next == TOKEN_KEY
            ? STATE_KEY
            : (next == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  case STATE_ENDITEM:
    next = _item_reader_next_impl(self, src);
    assert(next == TOKEN_STARTITEM || next == TOKEN_ENDSQITEM);
    self->current_item_state =
        next == TOKEN_STARTITEM
            ? STATE_STARTITEM
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_ENDSEQUENCE: // re-enter case
    next = _item_reader_next_impl(self, src);
    assert(next == TOKEN_KEY || next == TOKEN_ENDITEM);
    self->current_item_state =
        next == TOKEN_KEY
            ? STATE_KEY
            : (next == TOKEN_ENDITEM ? STATE_ENDITEM : STATE_INVALID);
    break;
  default:
    assert(0);
  }
  const enum dicm_event_type event_next = token2event(next);
  return event_next;
}

int _item_writer_next_token(struct _item_writer *self, struct dicm_dst *dst,
                            const enum dicm_event_type next) {
  const enum dicm_state current_state = self->current_item_state;
  const enum dicm_token token = event2token(next);
  int ret = -1;
  switch (current_state) {
  case STATE_STARTSEQUENCE:
    assert(token == TOKEN_STARTITEM || token == TOKEN_ENDSQITEM);
    if (token == TOKEN_STARTITEM) {
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_STARTITEM);
        _ide_set_vl(&ude, VL_UNDEFINED);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_STARTITEM;
      ret = 0;
    } else {
      assert(token == TOKEN_ENDSQITEM);
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_ENDSQITEM);
        _ide_set_vl(&ude, 0);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_ENDSEQUENCE;
      ret = 0;
    }
    break;
  case STATE_STARTITEM:
    assert(token == TOKEN_KEY);
    ret = _item_writer_next_token_impl(self, dst, token);
    assert(ret == 0);
    self->current_item_state = STATE_KEY;
    ret = 0;
    break;
  case STATE_KEY:
    assert(token == TOKEN_VALUE || TOKEN_STARTSEQUENCE);
#if 0
    assert(self->value_length_pos != VL_UNDEFINED);
    assert(self->value_length_pos <= self->da.vl);
    if (self->value_length_pos == self->da.vl)
      self->current_item_state = STATE_VALUE;
    else {
      assert(0);
    }
#endif
    if (token == TOKEN_VALUE) {
      self->current_item_state = STATE_VALUE;
      ret = 0;
    } else if (token == TOKEN_STARTSEQUENCE) {
      self->da.vl = VL_UNDEFINED;
      const bool enc = dicm_attribute_is_encapsulated_pixel_data(&self->da);
      {
        union _ude ude;
        _ede32_set_vl(&ude, VL_UNDEFINED);
        int err = dicm_dst_write(dst, &ude.ede32.vl, 4);
        assert(err == 4);
      }
      self->current_item_state =
          enc ? STATE_STARTFRAGMENTS : STATE_STARTSEQUENCE;
      ret = 0;
    } else {
      assert(0);
    }
    break;
  case STATE_VALUE:
    if (token == TOKEN_KEY) {
      ret = _item_writer_next_token_impl(self, dst, token);
      assert(ret == 0);
      self->current_item_state = STATE_KEY;
      ret = 0;
    } else {
      assert(token == TOKEN_ENDITEM);
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_ENDITEM);
        _ide_set_vl(&ude, 0);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_ENDITEM;
      ret = 0;
    }
    break;
  case STATE_ENDITEM:
    if (token == TOKEN_STARTITEM) {
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_STARTITEM);
        _ide_set_vl(&ude, VL_UNDEFINED);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_STARTITEM;
      ret = 0;
    } else {
      assert(token == TOKEN_ENDSQITEM);
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_ENDSQITEM);
        _ide_set_vl(&ude, 0);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_ENDSEQUENCE;
      ret = 0;
    }
    break;
  case STATE_ENDSEQUENCE:
    assert(token == TOKEN_ENDITEM || token == TOKEN_KEY);
    if (token == TOKEN_ENDITEM) {
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_ENDITEM);
        _ide_set_vl(&ude, 0);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_ENDITEM;
      ret = 0;
    } else {
      assert(token == TOKEN_KEY);
      ret = _item_writer_next_token_impl(self, dst, token);
      assert(ret == 0);
      self->current_item_state = STATE_KEY;
      ret = 0;
    }
    break;
  default:
    assert(0);
    break;
  }
  return ret;
}

static inline enum dicm_token
_fragments_reader_next_impl2(struct _item_reader *self) {
  const dicm_vr_t vr = self->da.vr;
  assert(vr == VR_NONE);
  self->value_length_pos = 0;

  return TOKEN_VALUE;
}

static enum dicm_token _fragments_reader_next_impl(struct _item_reader *self,
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
enum dicm_event_type _fragments_reader_next_event(struct _item_reader *self,
                                                  struct dicm_src *src) {
  const enum dicm_state current_state = self->current_item_state;
  enum dicm_token next;
  switch (current_state) {
  case STATE_STARTFRAGMENTS:
    next = _fragments_reader_next_impl(self, src);
    // FIXME: technically TOKEN_ENDSQITEM is impossible right here, this would
    // mean a duplicate Pixel Data was sent in the dataset.
    assert(next == TOKEN_FRAGMENT || next == TOKEN_ENDSQITEM);
    self->current_item_state =
        next == TOKEN_FRAGMENT
            ? STATE_FRAGMENT
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_FRAGMENT:
    next = _fragments_reader_next_impl2(self);
    assert(next == TOKEN_VALUE);
    self->current_item_state =
        next == TOKEN_VALUE ? STATE_VALUE : STATE_INVALID;
    break;
  case STATE_VALUE:
    // TODO: check user has consumed everything
    assert(self->da.vl == self->value_length_pos);
    next = _fragments_reader_next_impl(self, src);
    assert(next == TOKEN_FRAGMENT || next == TOKEN_ENDSQITEM);
    self->current_item_state =
        next == TOKEN_FRAGMENT
            ? STATE_FRAGMENT
            : (next == TOKEN_ENDSQITEM ? STATE_ENDSEQUENCE : STATE_INVALID);
    break;
  case STATE_ENDSEQUENCE:
    // it is impossible to nest sequence of fragments
    assert(0);
    break;
  default:
    assert(0);
  }
  const enum dicm_event_type event_next = token2event(next);
  return event_next;
}

int _fragments_writer_next_token(struct _item_writer *self,
                                 struct dicm_dst *dst,
                                 const enum dicm_event_type next) {
  const enum dicm_state current_state = self->current_item_state;
  const enum dicm_token token = event2token(next);
  int ret = -1;
  switch (current_state) {
  case STATE_STARTFRAGMENTS:
    assert(token == TOKEN_FRAGMENT);
    {
      union _ude ude;
      _ide_set_tag(&ude, TAG_STARTITEM);
      int err = dicm_dst_write(dst, &ude.ide, 4);
      assert(err == 4);
    }
    self->current_item_state = STATE_FRAGMENT;
    ret = 0;
    break;
  case STATE_FRAGMENT:
    assert(token == TOKEN_VALUE);
    assert(self->value_length_pos != VL_UNDEFINED);
    assert(self->value_length_pos <= self->da.vl);
    if (self->value_length_pos == self->da.vl)
      self->current_item_state = STATE_VALUE;
    else {
      assert(0);
    }
    ret = 0;
    break;
  case STATE_VALUE:
    if (token == TOKEN_FRAGMENT) {
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_STARTITEM);
        int err = dicm_dst_write(dst, &ude.ide, 4);
        assert(err == 4);
      }
      self->current_item_state = STATE_FRAGMENT;
      ret = 0;
    } else {
      assert(token == TOKEN_ENDSQITEM);
      {
        union _ude ude;
        _ide_set_tag(&ude, TAG_ENDSQITEM);
        _ide_set_vl(&ude, 0);
        int err = dicm_dst_write(dst, &ude.ide, 8);
        assert(err == 8);
      }
      self->current_item_state = STATE_ENDSEQUENCE;
      ret = 0;
    }
    break;
  case STATE_ENDSEQUENCE:
    // it is impossible to nest sequence of fragments
    assert(0);
  default:
    assert(0);
    break;
  }

  return ret;
}
