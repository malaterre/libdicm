#ifndef DICM_PRIVATE_H
#define DICM_PRIVATE_H

#include "dicm.h"

#ifdef __GNUC__
#define DICM_UNUSED __attribute__((__unused__))
#define DICM_PACKED __attribute__((packed))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* FIXME: not portable */
#include <byteswap.h>

/* common object private vtable */
struct object;
struct object_prv_vtable {
  DICM_CHECK_RETURN
  int (*fp_destroy)(struct object *const) DICM_NONNULL();
};

struct object_vtable {
  struct object_prv_vtable const obj;
};

struct object {
  struct object_vtable const *vtable;
};

/* common object interface */
#define object_destroy(t) ((t)->vtable->obj.fp_destroy((t)))

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MAKE_VR(left, right) (right << 8 | left)
#else
#define MAKE_VR(left, right) ((left << 8 | right) << 16)
#endif

enum VALUE_REPRESENTATIONS {
  VR_NONE = 0,
  VR_AE = MAKE_VR('A', 'E'),
  VR_AS = MAKE_VR('A', 'S'),
  VR_AT = MAKE_VR('A', 'T'),
  VR_CS = MAKE_VR('C', 'S'),
  VR_DA = MAKE_VR('D', 'A'),
  VR_DS = MAKE_VR('D', 'S'),
  VR_DT = MAKE_VR('D', 'T'),
  VR_FL = MAKE_VR('F', 'L'),
  VR_FD = MAKE_VR('F', 'D'),
  VR_IS = MAKE_VR('I', 'S'),
  VR_LO = MAKE_VR('L', 'O'),
  VR_LT = MAKE_VR('L', 'T'),
  VR_OB = MAKE_VR('O', 'B'),
  VR_OD = MAKE_VR('O', 'D'),
  VR_OF = MAKE_VR('O', 'F'),
  VR_OL = MAKE_VR('O', 'L'),
  VR_OV = MAKE_VR('O', 'V'),
  VR_OW = MAKE_VR('O', 'W'),
  VR_PN = MAKE_VR('P', 'N'),
  VR_SH = MAKE_VR('S', 'H'),
  VR_SL = MAKE_VR('S', 'L'),
  VR_SQ = MAKE_VR('S', 'Q'),
  VR_SS = MAKE_VR('S', 'S'),
  VR_ST = MAKE_VR('S', 'T'),
  VR_SV = MAKE_VR('S', 'V'),
  VR_TM = MAKE_VR('T', 'M'),
  VR_UC = MAKE_VR('U', 'C'),
  VR_UI = MAKE_VR('U', 'I'),
  VR_UL = MAKE_VR('U', 'L'),
  VR_UN = MAKE_VR('U', 'N'),
  VR_UR = MAKE_VR('U', 'R'),
  VR_US = MAKE_VR('U', 'S'),
  VR_UT = MAKE_VR('U', 'T'),
  VR_UV = MAKE_VR('U', 'V'),
};

static inline bool _is_vr16(const uint32_t vr) {
  switch (vr) {
  case VR_AE:
  case VR_AS:
  case VR_AT:
  case VR_CS:
  case VR_DA:
  case VR_DS:
  case VR_DT:
  case VR_FD:
  case VR_FL:
  case VR_IS:
  case VR_LO:
  case VR_LT:
  case VR_PN:
  case VR_SH:
  case VR_SL:
  case VR_SS:
  case VR_ST:
  case VR_TM:
  case VR_UI:
  case VR_UL:
  case VR_US:
    return true;
  }
  return false;
}

struct _ede32 {
  uint32_t tag;
  uint32_t vr;
  uint32_t vl;
}; // explicit data element. 12 bytes

struct _ede16 {
  uint32_t tag;
  uint16_t vr16;
  uint16_t vl16;
}; // explicit data element, VR/VL 16. 8 bytes

struct _ide {
  uint32_t tag;
  uint32_t vl;
}; // implicit data element. 8 bytes

union _ude {
  uint32_t bytes[4];   // 16 bytes, 32bits aligned
  struct _ede32 ede32; // explicit data element (12 bytes)
  struct _ede16 ede16; // explicit data element (8 bytes)
  struct _ide ide;     // implicit data element (8 bytes)
};

struct _ede {
  union {
    struct {
      uint32_t tag1;
      uint32_t vr32;
      uint32_t vl32;
    }; // explicit data element. 12 bytes
    struct {
      uint32_t tag2;
      uint16_t vr16;
      uint16_t vl16;
    }; // explicit data element, VR/VL 16. 8 bytes
    uint32_t tag;
  };
};

struct dual {
  union {
    struct _ede evr; // explicit data element (8/12 bytes)
    struct _ide ivr; // implicit data element (8 bytes)
  };
};

/*
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SWAP_TAG(t) bswap_32(t)
#else
#error
#endif
*/

static inline uint32_t _ide_get_tag(const union _ude *ude) {
  // byte-swap tag:
  union {
    uint32_t t;
    uint16_t a[2];
  } u;
  u.t = ude->ide.tag;
  return (uint32_t)((uint32_t)u.a[0] << 16u | u.a[1]);
}
static inline void _ide_set_tag(union _ude *ude, const uint32_t tag) {
  // byte-swap tag:
  union {
    uint32_t t;
    uint16_t a[2];
  } u;
  u.t = tag;
  ude->ide.tag = (uint32_t)((uint32_t)u.a[0] << 16u | u.a[1]);
}

static inline uint32_t _ide_get_vl(const union _ude *ude) {
  return ude->ide.vl;
}
static inline void _ide_set_vl(union _ude *ude, const uint32_t vl) {
  ude->ide.vl = vl;
}

static inline uint32_t _ede16_get_vr(const union _ude *ude) {
  return ude->ede16.vr16;
}
static inline void _ede16_set_vr(union _ude *ude, const uint32_t vr) {
  ude->ede16.vr16 = (uint16_t)vr;
}
static inline void _ede32_set_vr(union _ude *ude, const uint32_t vr) {
  ude->ede32.vr = vr;
}

static inline uint32_t _ede16_get_vl(const union _ude *ude) {
  return ude->ede16.vl16;
}

static inline void _ede16_set_vl(union _ude *ude, const uint32_t vl) {
  assert(vl <= UINT16_MAX);
  ude->ede16.vl16 = (uint16_t)vl;
}

static inline uint32_t _ede32_get_vl(const union _ude *ude) {
  return ude->ede32.vl;
}
static inline void _ede32_set_vl(union _ude *ude, const uint32_t vl) {
  ude->ede32.vl = vl;
}

enum dicm_state {
  STATE_INVALID = -1,
  /* ready state (after invalid) */
  STATE_INIT = 0,
  /* document */
  STATE_STARTDOCUMENT,
  STATE_ENDDOCUMENT,
  /* key */
  STATE_KEY,
  /* fragment */
  STATE_FRAGMENT,
  /* fake state to simplify implementation */
  STATE_STARTFRAGMENTS,
  /* */
  STATE_VALUE,
  /* item */
  STATE_STARTITEM,
  STATE_ENDITEM,
  /* sequence */
  STATE_STARTSEQUENCE,
  STATE_ENDSEQUENCE,
};

enum dicm_token {
  /* key (data element tag+vr) */
  TOKEN_KEY = 0,
  /* data value, only when not undefined length */
  TOKEN_VALUE,
  TOKEN_FRAGMENT,
  /* fragments (encapsulated pixel data) */
  TOKEN_STARTFRAGMENTS,
  /* item start */
  TOKEN_STARTITEM,
  /* item end */
  TOKEN_ENDITEM,
  /* defined or undefined length sequence */
  TOKEN_STARTSEQUENCE,
  /* sq or fragments end */
  TOKEN_ENDSQITEM,
  /* end of file */
  TOKEN_EOF,
  /* invalid token */
  TOKEN_INVALID_DATA,
};

static inline enum dicm_event_type
state2event(const enum dicm_state new_state) {
  enum dicm_event_type next;
  switch (new_state) {
  case STATE_KEY:
    next = DICM_KEY_EVENT;
    break;
  case STATE_VALUE:
    next = DICM_VALUE_EVENT;
    break;
  case STATE_STARTSEQUENCE:
    next = DICM_SEQUENCE_START_EVENT;
    break;
  case STATE_ENDSEQUENCE:
    next = DICM_SEQUENCE_END_EVENT;
    break;
  case STATE_STARTFRAGMENTS:
    next = DICM_SEQUENCE_START_EVENT;
    break;
  case STATE_FRAGMENT:
    next = DICM_FRAGMENT_EVENT;
    break;
  case STATE_STARTITEM:
    next = DICM_ITEM_START_EVENT;
    break;
  case STATE_ENDITEM:
    next = DICM_ITEM_END_EVENT;
    break;
  case STATE_STARTDOCUMENT:
    next = DICM_DOCUMENT_START_EVENT;
    break;
  case STATE_ENDDOCUMENT:
    next = DICM_DOCUMENT_END_EVENT;
    break;
  default:
    assert(0);
  }
  return next;
}

static inline enum dicm_event_type
token2event(const enum dicm_token dicm_next) {
  enum dicm_event_type next;
  switch (dicm_next) {
  case TOKEN_KEY:
    next = DICM_KEY_EVENT;
    break;
  case TOKEN_VALUE:
    next = DICM_VALUE_EVENT;
    break;
  case TOKEN_FRAGMENT:
    next = DICM_FRAGMENT_EVENT;
    break;
  case TOKEN_STARTSEQUENCE:
  case TOKEN_STARTFRAGMENTS:
    next = DICM_SEQUENCE_START_EVENT;
    break;
  case TOKEN_ENDSQITEM:
    next = DICM_SEQUENCE_END_EVENT;
    break;
  case TOKEN_STARTITEM:
    next = DICM_ITEM_START_EVENT;
    break;
  case TOKEN_ENDITEM:
    next = DICM_ITEM_END_EVENT;
    break;
  default:
    assert(0);
  }
  return next;
}

static inline enum dicm_token
event2token(const enum dicm_event_type event_type) {
  enum dicm_token token;
  switch (event_type) {
  case DICM_KEY_EVENT:
    token = TOKEN_KEY;
    break;
  case DICM_VALUE_EVENT:
    token = TOKEN_VALUE;
    break;
  case DICM_FRAGMENT_EVENT:
    token = TOKEN_FRAGMENT;
    break;
  case DICM_SEQUENCE_START_EVENT:
    // token = TOKEN_STARTFRAGMENTS;
    token = TOKEN_STARTSEQUENCE;
    break;
  case DICM_SEQUENCE_END_EVENT:
    token = TOKEN_ENDSQITEM;
    break;
  case DICM_ITEM_START_EVENT:
    token = TOKEN_STARTITEM;
    break;
  case DICM_ITEM_END_EVENT:
    token = TOKEN_ENDITEM;
    break;
#if 1
  case DICM_DOCUMENT_END_EVENT:
    token = TOKEN_EOF;
    break;
#endif
  default:
    assert(0);
  }
  return token;
}

/* helpers */
static inline bool is_aligned(const void *restrict pointer, size_t byte_count) {
  return (uintptr_t)pointer % byte_count == 0;
}

#endif /* DICM_PRIVATE_H */
