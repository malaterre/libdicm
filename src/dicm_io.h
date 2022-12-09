#ifndef DICM_IO_H
#define DICM_IO_H

#include "dicm_private.h"

#include <stddef.h> /* size_t */
#include <stdio.h>  /* FILE */

#if 0
struct dicm_io {
  // According to POSIX.1, if count is greater than SSIZE_MAX, the result
  // is implementation-defined; see NOTES for the upper limit on Linux.
  //
  // On  Linux,  read()  (and  similar system calls) will transfer at
  // most 0x7ffff000 (2,147,479,552) bytes, returning the number of bytes
  // actually transferred.  (This is true on both 32-bit and 64-bit
  // systems.)
  DICM_CHECK_RETURN int (*fp_read)(struct dicm_io *const, void *,
                                   size_t) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_write)(struct dicm_io *const, const void *,
                                    size_t) DICM_NONNULL;
};
#endif

struct io_prv_vtable {
  DICM_CHECK_RETURN int (*fp_read)(void *const, void *, size_t) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_seek)(void *const, long, int) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_write)(void *const, const void *,
                                    size_t) DICM_NONNULL;
};

/* common io vtable */
struct io_vtable {
  struct object_prv_vtable const object;
  struct io_prv_vtable /*const*/ io;
};

/* common io object */
struct dicm_io {
  struct io_vtable const *vtable;
};

/* common io interface */
#define dicm_io_read(t, b, s) ((t)->vtable->io.fp_read((t), (b), (s)))
#define dicm_io_seek(t, b, s) ((t)->vtable->io.fp_seek((t), (b), (s)))
#define dicm_io_write(t, b, s) ((t)->vtable->io.fp_write((t), (b), (s)))

#endif /* DICM_IO_H */
