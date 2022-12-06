#ifndef DICM_ITEM_H
#define DICM_ITEM_H

#include "dicm.h"

#include "dicm_private.h"

#include <stdint.h>
#include <stdlib.h> /* malloc */
#include <string.h>

struct _attribute {
  /* the current tag */
  uint32_t tag;
  /* the current vr */
  uint32_t vr;
  /* the current vl */
  uint32_t vl;
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

struct _item_reader {
  /* the current item state */
  enum dicm_state current_item_state;

  struct _attribute da;

  /* current pos in value_length */
  uint32_t value_length_pos;

  DICM_CHECK_RETURN int (*fp_next_event)(struct _item_reader *self,
                                         struct dicm_io *src);
};

/* group key */

typedef uint32_t dicm_tag_t;
typedef uint32_t dicm_vr_t;

struct dicm_key {
  dicm_tag_t tag;
  dicm_vr_t vr;
};

/* tag */

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

/* Convert the integer VR representation into a c-string (ASCII) NULL terminated
 */
#define dicm_vr_get_string(vr) ((const char *)&vr)

bool dicm_vr_is_16(dicm_vr_t vr);

/* vl */
typedef uint32_t dicm_vl_t;

enum VALUE_LENGTHS { VL_UNDEFINED = 0xffffffff };

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
 * implementation of is_root.
 */
struct array {
  size_t size;
  size_t capacity;
  struct _item_reader *data;
};

static inline struct array *array_create(struct array *arr, size_t size) {
  assert(arr);
  arr->size = size;
  arr->capacity = size;
  if (size) {
    arr->data = malloc(size * sizeof(struct _item_reader));
  } else {
    arr->data = NULL;
  }
  return arr;
}

static inline void array_free(struct array *arr) { free(arr->data); }

static inline struct _item_reader *array_at(struct array *arr,
                                            const size_t index) {
  if (index < arr->size)
    return &arr->data[index];
  return NULL;
}

static inline void array_push_back(struct array *arr,
                                   struct _item_reader *item_reader) {
  arr->size++;
  if (arr->size >= arr->capacity) {
    arr->capacity = 2 * arr->size;
    arr->data = realloc(arr->data, arr->capacity * sizeof(struct _item_reader));
    memcpy(&arr->data[arr->size - 1], item_reader, sizeof(struct _item_reader));
  } else {
    memcpy(&arr->data[arr->size - 1], item_reader, sizeof(struct _item_reader));
  }
}

static inline struct _item_reader *array_back(struct array *arr) {
  struct _item_reader *item_reader = &arr->data[arr->size - 1];
  return item_reader;
}

static inline void array_pop_back(struct array *arr) {
  assert(arr->size);
  arr->size--;
}

#endif /* DICM_ITEM_H */
