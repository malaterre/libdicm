#ifndef DICM_EMITTER_H
#define DICM_EMITTER_H

#include "dicm.h"

#include "dicm_private.h"

struct dicm_emitter;

struct emitter_prv_vtable {
  /* kKey */
  DICM_CHECK_RETURN int (*fp_write_key)(struct dicm_emitter *,
                                        const struct dicm_key *) DICM_NONNULL;

  /* kValue: valid for both attribute and fragment */
  DICM_CHECK_RETURN int (*fp_write_value_length)(struct dicm_emitter *,
                                                 const size_t *) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_write_value)(struct dicm_emitter *, const void *,
                                          size_t) DICM_NONNULL;

  /* kFragment */
  DICM_CHECK_RETURN int (*fp_write_fragment)(struct dicm_emitter *)
      DICM_NONNULL;

  /* kItem */
  DICM_CHECK_RETURN int (*fp_write_start_item)(struct dicm_emitter *)
      DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_write_end_item)(struct dicm_emitter *)
      DICM_NONNULL;

  /* kSequence: valid for SQ and Pixel Data,OB,u/l */
  DICM_CHECK_RETURN int (*fp_write_start_sequence)(struct dicm_emitter *)
      DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_write_end_sequence)(struct dicm_emitter *)
      DICM_NONNULL;

  /* We need a start model to implement easy conversion to XML */
  DICM_CHECK_RETURN int (*fp_write_start_dataset)(struct dicm_emitter *,
                                                  const char *) DICM_NONNULL;
  DICM_CHECK_RETURN int (*fp_write_end_dataset)(struct dicm_emitter *)
      DICM_NONNULL;
};

/* common emitter vtable */
struct emitter_vtable {
  struct object_prv_vtable const object;
  struct emitter_prv_vtable const emitter;
};

/* common emitter object */
struct dicm_emitter {
  struct emitter_vtable const *vtable;
};

#endif /* DICM_EMITTER_H */
