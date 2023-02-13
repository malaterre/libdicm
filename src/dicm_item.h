#ifndef DICM_ITEM_H
#define DICM_ITEM_H

#include "dicm.h"

#include "dicm_private.h"

#include <stdint.h>
#include <string.h>

struct _attribute {
  /* the current tag */
  uint32_t tag;
  /* the current vr */
  uint32_t vr;
  /* the current vl */
  uint32_t vl;
  /* alignement, not used */
  uint32_t reserved;
};

static inline bool _ude_init(union _ude *ude, const struct _attribute *da) {
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

struct ivr {
  union {
    uint32_t bytes[4]; // 8 bytes, 32bits aligned
    struct {
      uint32_t tag, vl;
    };
  };
};

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

static inline struct evr _evr_init1(const struct _attribute *da) {
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

static inline struct ivr _ivr_init1(const struct _attribute *da) {
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

static inline struct evr _evr_init2(const struct _attribute *da) {
  struct evr evr;
  evr.tag = bswap_32(da->tag);
  const uint32_t vr = da->vr;
  const bool is_vr16 = _is_vr16(vr);
  evr.vr = vr;
  evr.vl = is_vr16 ? bswap_16(da->vl) : bswap_32(da->vl);
  evr.vr_size = is_vr16 ? 2 : 4;
  return evr;
}

static inline struct ivr _ivr_init2(const struct _attribute *da) {
  struct ivr ivr;
  ivr.tag = bswap_32(da->tag);
  ivr.vl = bswap_32(da->vl);
  return ivr;
}

struct _item_reader;
struct _item_reader_prv_vtable {
  /* before reading key */
  DICM_CHECK_RETURN enum dicm_token (*fp_key_token)(struct _item_reader *self,
                                                    struct dicm_src *src);
  /* before reading value (after key) */
  DICM_CHECK_RETURN enum dicm_token (*fp_value_token)(struct _item_reader *self,
                                                      struct dicm_src *src);

  /* compute new state from current state + event */
  DICM_CHECK_RETURN enum dicm_state (*fp_next_event)(
      struct _item_reader *self, const enum dicm_state current_state,
      struct dicm_src *src);

  /* return next level emitter */
  struct _item_reader (*fp_next_level)(struct _item_reader *self,
                                       enum dicm_state new_state) DICM_NONNULL;
};
struct _item_reader_vtable {
  struct _item_reader_prv_vtable const reader;
};
struct _item_reader {
  struct _attribute da;

  struct _item_reader_vtable const *vtable;
};

// FIXME: rename to onelevel_writer or nested_emitter or sublevel_emitter
struct _item_writer;
struct _item_writer_prv_vtable {
  /* writing key */
  DICM_CHECK_RETURN enum dicm_state (*fp_key_token)(
      struct _item_writer *self, struct dicm_dst *dst,
      const enum dicm_token token);
  /* writing vl */
  DICM_CHECK_RETURN enum dicm_state (*fp_vl_token)(struct _item_writer *self,
                                                   struct dicm_dst *dst,
                                                   const enum dicm_token token);
  /* writing value */
  DICM_CHECK_RETURN enum dicm_state (*fp_value_token)(
      struct _item_writer *self, struct dicm_dst *dst,
      const enum dicm_token token);

  /* compute new state from current state + event */
  DICM_CHECK_RETURN enum dicm_state (*fp_next_event)(
      struct _item_writer *self, const enum dicm_state current_state,
      struct dicm_dst *dst, const enum dicm_event_type next);

  /* return next level emitter */
  struct _item_writer (*fp_next_level)(struct _item_writer *self,
                                       enum dicm_state new_state) DICM_NONNULL;
};
struct _item_writer_vtable {
  struct _item_writer_prv_vtable const writer;
};
struct _item_writer {
  struct _attribute da;
  /* FIXME: item number book-keeping */
  struct _item_writer_vtable const *vtable;
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
dicm_attribute_is_encapsulated_pixel_data(const struct _attribute *da) {
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
