#ifndef DICM_ITEM_H
#define DICM_ITEM_H

#include "dicm.h"

#include "dicm_private.h"

#include <stdint.h>
#include <string.h>

struct key_info {
  /* the current tag */
  uint32_t tag;
  /* the current vr */
  uint32_t vr;
  /* the current vl */
  uint32_t vl;
  /* alignement, not used */
  uint32_t reserved;
};

static inline bool _ude_init(union _ude *ude, const struct key_info *da) {
  _ide_set_tag(ude, da->tag);
  const bool is_vr16 = _is_vr16(da->vr);
  if (is_vr16) {
    _ede16_set_vr(ude, da->vr);
    _ede16_set_vl(ude, da->vl);
  } else {
    _ede32_set_vr(ude, da->vr);
    _ede32_set_vl(ude, da->vl);
  }

  return is_vr16;
}

// https://en.cppreference.com/w/c/language/union
// c11 allows anonymous union/structure:
struct evr {
  union {
    uint32_t bytes[4]; // 16 bytes, 32bits aligned
    struct {
      uint32_t tag, vr, vl, vr_size;
    };
  };
};

#if 1
struct ivr {
  union {
    uint32_t bytes[4]; // 8 bytes, 32bits aligned
    struct {
      uint32_t tag, vl;
    };
  };
};
#endif

// struct dual {
//	union {
//	struct evr evr;
//	struct ivr ivr;
//	};
//};

static inline uint32_t evr_get_key_size(struct evr evr) {
  return 4 + evr.vr_size;
}

static inline uint32_t evr_get_size(struct evr evr) {
  return 4 + 2 * evr.vr_size;
}

static inline struct evr _evr_init1(const struct key_info *da) {
  struct evr evr;
  _Static_assert(16 == sizeof(evr), "16 bytes");
#if 0
  evr.tag = da->tag;
#else
  union {
    uint32_t t;
    uint16_t a[2];
  } u;
  u.t = da->tag;
  evr.tag = (uint32_t)((uint32_t)u.a[0] << 16u | u.a[1]);
#endif
  const uint32_t vr = da->vr;
  const bool is_vr16 = _is_vr16(vr);
  evr.vr = vr;
  evr.vl = is_vr16 ? da->vl : da->vl;
  evr.vr_size = is_vr16 ? 2 : 4;
  return evr;
}

static inline struct ivr _ivr_init1(const struct key_info *da) {
  struct ivr ivr;
  union {
    uint32_t t;
    uint16_t a[2];
  } u;
  u.t = da->tag;
  ivr.tag = (uint32_t)((uint32_t)u.a[0] << 16u | u.a[1]);
  ivr.vl = da->vl;
  return ivr;
}

static inline struct evr _evr_init2(const struct key_info *da) {
  struct evr evr;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  evr.tag = bswap_32(da->tag);
#else
  evr.tag = da->tag;
#endif
  const uint32_t vr = da->vr;
  const bool is_vr16 = _is_vr16(vr);
  evr.vr = vr;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  evr.vl = is_vr16 ? bswap_16((uint16_t)da->vl) : bswap_32(da->vl);
#else
  evr.vl = is_vr16 ? da->vl : da->vl;
#endif
  evr.vr_size = is_vr16 ? 2 : 4;
  return evr;
}

static inline struct ivr _ivr_init2(const struct key_info *da) {
  struct ivr ivr;
  ivr.tag = bswap_32(da->tag);
  ivr.vl = bswap_32(da->vl);
  return ivr;
}

struct level_parser;
struct level_parser_prv_vtable {
  /* structure */
  DICM_CHECK_RETURN enum token (*fp_structure_read_key)(
      struct level_parser *const self, struct dicm_src *src) DICM_NONNULL();

  /* before reading value (after key) */
  DICM_CHECK_RETURN enum token (*fp_value_token)(struct level_parser *self,
                                                 struct dicm_src *src)
      DICM_NONNULL();

  /* compute new state from current state + event */
  DICM_CHECK_RETURN enum state (*fp_next_event)(struct level_parser *self,
                                                const enum state current_state,
                                                struct dicm_src *src)
      DICM_NONNULL();

  /* return next level emitter */
  struct level_parser (*fp_next_level)(struct level_parser *self,
                                       enum state new_state) DICM_NONNULL();
};
struct level_parser_vtable {
  struct level_parser_prv_vtable const reader;
};
struct sequence_read_prv_vtable {
  /* read from src inside a defined/undefined sequence */
  DICM_CHECK_RETURN enum token (*fp_end_sequence_token)(
      struct level_parser *const self, struct dicm_src *src) DICM_NONNULL();
};
struct sequence_read_vtable {
  struct sequence_read_prv_vtable const sequence;
};
struct item_read_prv_vtable {
  /* read from src inside a defined/undefined item */
  DICM_CHECK_RETURN enum token (*fp_end_item_token)(
      struct level_parser *const self, struct dicm_src *src) DICM_NONNULL();
};
struct item_read_vtable {
  struct item_read_prv_vtable const item;
};

struct level_parser {
  struct key_info da;

  struct level_parser_vtable const *vtable;
  /* region sequence */

  /* current pos in sequence_length */
  uint32_t sequence_length2;
  uint32_t sequence_length_pos;

  struct sequence_read_vtable const *vtable2;
  /* end region */

  /* item */
  uint32_t item_length_pos;
  uint32_t item_length2;
  struct item_read_vtable const *vtable3;
  /* end item */
};

struct level_emitter;
struct level_emitter_prv_vtable {

  DICM_CHECK_RETURN enum token (*fp_structure_key_token)(
      struct level_emitter *const self, struct dicm_dst *dst,
      const enum dicm_event_type next) DICM_NONNULL();
  /* writing vl */
  DICM_CHECK_RETURN enum token (*fp_vl_token)(struct level_emitter *const self,
                                              struct dicm_dst *dst,
                                              const enum dicm_event_type next)
      DICM_NONNULL();
  /* writing value */
  DICM_CHECK_RETURN enum token (*fp_value_token)(
      struct level_emitter *const self, struct dicm_dst *dst,
      const enum dicm_event_type next) DICM_NONNULL();

  /* compute new state from current state + event */
  DICM_CHECK_RETURN enum state (*fp_next_event)(
      struct level_emitter *const self, const enum state current_state,
      struct dicm_dst *dst, const enum dicm_event_type next) DICM_NONNULL();

  /* return next level emitter */
  struct level_emitter (*fp_next_level)(struct level_emitter *const self,
                                        enum state new_state) DICM_NONNULL();
};
struct sequence_prv_vtable {
  /* writing key */
  DICM_CHECK_RETURN enum token (*fp_end_sequence_token)(
      struct level_emitter *const self, struct dicm_dst *dst,
      const enum dicm_event_type next) DICM_NONNULL();
};
struct item_prv_vtable {
  /* writing key */
  DICM_CHECK_RETURN enum token (*fp_end_item_token)(
      struct level_emitter *const self, struct dicm_dst *dst,
      const enum dicm_event_type next) DICM_NONNULL();
};
struct level_emitter_vtable {
  struct level_emitter_prv_vtable const level_emitter;
};
struct sequence_vtable {
  struct sequence_prv_vtable const sequence;
};
struct item_vtable {
  struct item_prv_vtable const item;
};
struct level_emitter {
  struct key_info da;
  /* FIXME: item number book-keeping */
  struct level_emitter_vtable const
      *vtable; // FIXME rename to 'structure_emitter'

  /* region sequence */

  /* current pos in sequence_length */
  uint32_t sequence_length2;
  uint32_t sequence_length_pos;

  struct sequence_vtable const *vtable2;
  /* end region */

  /* item */
  uint32_t item_length_pos;
  uint32_t item_length2;
  struct item_vtable const *vtable3;
  /* end item */
};

/* tag */
typedef uint32_t dicm_tag_t;

/* Retrieve the group part from a tag */
static inline uint_fast16_t dicm_tag_get_group(const dicm_tag_t tag) {
  return (uint16_t)(tag >> 16u);
}
/* Retrieve the element part from a tag */
static inline uint_fast16_t dicm_tag_get_element(const dicm_tag_t tag) {
  return (uint16_t)(tag & 0x0000ffff);
}

#define MAKE_TAG(group, element) (uint32_t)(group << 16u | element)

static inline dicm_tag_t dicm_tag_set_group(const dicm_tag_t tag,
                                            const uint_fast16_t group) {
  const uint_fast16_t element = dicm_tag_get_element(tag);
  return MAKE_TAG(group, element);
}
static inline dicm_tag_t dicm_tag_set_element(const dicm_tag_t tag,
                                              const uint_fast16_t element) {
  const uint_fast16_t group = dicm_tag_get_group(tag);
  return MAKE_TAG(group, element);
}

enum SPECIAL_TAGS {
  TAG_PIXELDATA = MAKE_TAG(0x7fe0, 0x0010),
  TAG_DATASETTRAILINGPADDING = MAKE_TAG(0xfffc, 0xfffc),
  TAG_STARTITEM = MAKE_TAG(0xfffe, 0xe000),
  TAG_ENDITEM = MAKE_TAG(0xfffe, 0xe00d),
  TAG_ENDSQITEM = MAKE_TAG(0xfffe, 0xe0dd),
};

static inline bool dicm_tag_is_start_item(const dicm_tag_t tag) {
  return tag == TAG_STARTITEM;
}
static inline bool dicm_tag_is_end_item(const dicm_tag_t tag) {
  return tag == TAG_ENDITEM;
}
static inline bool dicm_tag_is_end_sq_item(const dicm_tag_t tag) {
  return tag == TAG_ENDSQITEM;
}
static inline bool dicm_tag_is_private(const dicm_tag_t tag) {
  const uint_fast16_t group = dicm_tag_get_group(tag);
  return group % 2 == 1;
}
static inline bool dicm_tag_is_group_length(const dicm_tag_t tag) {
  const uint_fast16_t element = dicm_tag_get_element(tag);
  return element == 0x0;
}

/* vr */
typedef uint32_t dicm_vr_t;

/* Convert the integer VR representation into a c-string (ASCII) NULL terminated
 */
#define dicm_vr_get_string(vr) ((const char *)&vr)

bool dicm_vr_is_16(dicm_vr_t vr);

/* vl */
typedef uint32_t dicm_vl_t;

/* Special Value Length */
enum VALUE_LENGTHS { VL_UNDEFINED = 0xffffffff };

static inline bool dicm_vl_is_even(const dicm_vl_t vl) { return vl % 2 == 0; }

static inline bool dicm_vl_is_undefined(const dicm_vl_t vl) {
  return vl == VL_UNDEFINED;
}

/* key */
static inline bool
dicm_attribute_is_encapsulated_pixel_data(const struct key_info *da) {
  // Make sure Pixel Data is Encapsulated (Sequence of Fragments):
  if (da->tag == TAG_PIXELDATA && da->vl == VL_UNDEFINED && da->vr == VR_OB) {
    return true;
  }
  return false;
}

/* item */

/* fragment */

/* Implementation details:
 * technically we could have used a single linked list since we only really
 * need push/pop but array have the extra properly of being close to each
 * other, and we also get the size property for free which allow an easy
 * implementation of is_root. Pay attention that JPEG TS is special since only
 * the root Pixel Data must be undefined length
 */
// https://stackoverflow.com/questions/9722632/what-happens-if-i-define-a-0-size-array-in-c-c
// https://en.wikipedia.org/wiki/Flexible_array_member
// https://tiehu.is/blog/c1

#define ARRAY_LEN(a) (sizeof(a) / sizeof(*(a)))

#define array_base                                                             \
  struct {                                                                     \
    size_t capacity, size;                                                     \
  }

#define array(T)                                                               \
  struct array_##T {                                                           \
    array_base;                                                                \
    T data[];                                                                  \
  }

#define array_new(T, v)                                                        \
  do {                                                                         \
    const size_t initial_size = 1;                                             \
    (v) = malloc(sizeof(array(T)) + sizeof(T) * initial_size);                 \
    (v)->capacity = initial_size;                                              \
    (v)->size = 0;                                                             \
  } while (0)

#define array_free(v) free((v))

#define array_ref(v, i) (&(v)->data[i])

#define array_at(v, i) (*(array_ref((v), i)))

#define array_push(v, i)                                                       \
  do {                                                                         \
    if ((v)->size >= (v)->capacity) {                                          \
      (v)->capacity *= 2;                                                      \
      (v) = realloc((v),                                                       \
                    sizeof(array_base) + (v)->capacity * sizeof(*(v)->data));  \
    }                                                                          \
    (v)->data[(v)->size++] = (i);                                              \
  } while (0)

#define array_back(v) (*(array_ref((v), (v)->size - 1)))

#define array_pop(v) ((v)->data[--(v)->size])

#endif /* DICM_ITEM_H */
