#ifndef DICM_SRC_H
#define DICM_SRC_H

#include "dicm_private.h"

#include <stddef.h> /* size_t */

struct src_prv_vtable {
  DICM_CHECK_RETURN int64_t (*fp_read)(struct dicm_src *, void *,
                                       size_t) DICM_NONNULL;
  DICM_CHECK_RETURN int64_t (*fp_seek)(struct dicm_src *, int64_t,
                                       int) DICM_NONNULL;
};

struct dicm_src_vtable {
  struct object_prv_vtable const obj;
  struct src_prv_vtable const src;
};

/* common src interface */
#define dicm_src_read(t, b, s) ((t)->vtable->src.fp_read((t), (b), (s)))
#define dicm_src_seek(t, b, s) ((t)->vtable->src.fp_seek((t), (b), (s)))

#endif /* DICM_SRC_H */
