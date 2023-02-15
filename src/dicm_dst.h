#ifndef DICM_DST_H
#define DICM_DST_H

#include "dicm_private.h"

#include <stddef.h> /* size_t */

struct dst_prv_vtable {
  DICM_CHECK_RETURN int64_t (*fp_write)(struct dicm_dst *, const void *, size_t)
      DICM_NONNULL();
  DICM_CHECK_RETURN int64_t (*fp_seek)(struct dicm_dst *, int64_t, int)
      DICM_NONNULL();
};

struct dicm_dst_vtable {
  struct object_prv_vtable const obj;
  struct dst_prv_vtable const dst;
};

/* common dst interface */
#define dicm_dst_write(t, b, s) ((t)->vtable->dst.fp_write((t), (b), (s)))

#endif /* DICM_DST_H */
