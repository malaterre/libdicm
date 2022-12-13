#ifndef DICM_DST_H
#define DICM_DST_H

#include "dicm_private.h"

#include <stddef.h> /* size_t */

struct dst_prv_vtable {
  DICM_CHECK_RETURN int (*fp_write)(struct dicm_dst *const, const void *,
                                    size_t) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_seek)(struct dicm_dst *const, long,
                                   int) DICM_NONNULL;
};

struct dicm_dst_vtable {
  struct object_prv_vtable const obj;
  struct dst_prv_vtable const dst;
};

/* common dst interface */
#define dicm_dst_write(t, b, s) ((t)->vtable->dst.fp_write((t), (b), (s)))

#endif /* DICM_DST_H */
