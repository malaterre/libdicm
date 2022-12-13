#include "dicm_private.h"

int dicm_delete(void *self_) {
  struct object *self = (struct object *)self_;
  if (!self->vtable->obj.fp_destroy) {
    return -1;
  }
  return object_destroy(self);
}
