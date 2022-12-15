#ifndef DICM_PARSER_H
#define DICM_PARSER_H

#include "dicm.h"

#include "dicm_item.h"
#include "dicm_private.h"

#include <stddef.h> /* size_t */

struct dicm_parser;
struct parser_prv_vtable {
  /* kKey */
  DICM_CHECK_RETURN int (*fp_get_key)(struct dicm_parser *,
                                      struct dicm_key *) DICM_NONNULL;

  /* kValue: valid for both data element and fragment */
  DICM_CHECK_RETURN int (*fp_get_value_length)(struct dicm_parser *,
                                               uint32_t *) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_read_value)(struct dicm_parser *, void *,
                                         size_t) DICM_NONNULL;
};

/* common parser vtable */
struct parser_vtable {
  struct object_prv_vtable const object;
  struct parser_prv_vtable const parser;
};

/* common parser object */
struct dicm_parser {
  struct parser_vtable const *vtable;
};

/* common parser interface */
#define dicm_parser_get_key1(t, dk) ((t)->vtable->parser.fp_get_key((t), (dk)))
#define dicm_parser_get_value_length1(t, s)                                    \
  ((t)->vtable->parser.fp_get_value_length((t), (s)))
#define dicm_parser_read_value1(t, b, s)                                       \
  ((t)->vtable->parser.fp_read_value((t), (b), (s)))

#endif /* DICM_PARSER_H */
