#include "dicm_src.h"

#include <stdio.h>  /* FILE */
#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

struct file {
  struct dicm_src super;
  /* data */
  FILE *stream;
};

static DICM_CHECK_RETURN int file_destroy(struct object *) DICM_NONNULL;
static DICM_CHECK_RETURN int file_read(struct dicm_src *const, void *,
                                       size_t) DICM_NONNULL;
static DICM_CHECK_RETURN int file_seek(struct dicm_src *const, long,
                                       int) DICM_NONNULL;

static struct dicm_src_vtable const g_file_vtable = {
    .obj = {.fp_destroy = file_destroy},
    .src = {.fp_read = file_read, .fp_seek = file_seek}};

static struct dicm_src_vtable const g_stdstream_vtable = {
    .obj = {.fp_destroy = file_destroy},
    .src = {.fp_read = file_read, .fp_seek = NULL}};

int file_destroy(struct object *obj) {
  struct file *self = (struct file *)obj;
  free(self);
  return 0;
}

int file_read(struct dicm_src *const src, void *buf, size_t size) {
  struct file *self = (struct file *)src;
  const size_t read = fread(buf, 1, size, self->stream);
  /* fread() does not distinguish between end-of-file and error, and callers
   * must use feof(3) and ferror(3) to determine which occurred. */
  if (read != size) {
    const int eof = feof(self->stream);
    const int error = ferror(self->stream);
    if (error)
      return -1;
    assert(eof && read == 0);
  }
  const int ret = (int)read;
  assert(ret >= 0);
  return ret;
}

int file_seek(struct dicm_src *const src, long offset, int whence) {
  struct file *self = (struct file *)src;
  const int ret = fseek(self->stream, offset, whence);
  return ret;
}

// https://stackoverflow.com/questions/58670828/is-there-a-way-to-rewind-stdin-in-c
static inline bool is_stream_seekable(FILE *stream) {
  if (fseek(stream, 0L, SEEK_SET)) {
    return false;
  }
  return true;
}

int dicm_src_file_create(struct dicm_src **pself, FILE *stream) {
  assert(stream);
  struct file *self = (struct file *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->super;
    self->super.vtable =
        is_stream_seekable(stream) ? &g_file_vtable : &g_stdstream_vtable;
    self->stream = stream;
    return 0;
  }
  *pself = NULL;
  return -1;
}

struct mem {
  struct dicm_src super;
  /* data */
  const void *cur;
  const void *beg;
  const void *end;
};

static DICM_CHECK_RETURN int mem_destroy(struct object *) DICM_NONNULL;
static DICM_CHECK_RETURN int mem_read(struct dicm_src *const, void *,
                                      size_t) DICM_NONNULL;
static DICM_CHECK_RETURN int mem_seek(struct dicm_src *const, long,
                                      int) DICM_NONNULL;

static struct dicm_src_vtable const g_mem_vtable = {
    .obj = {.fp_destroy = mem_destroy},
    .src = {.fp_read = mem_read, .fp_seek = mem_seek}};

int mem_destroy(struct object *obj) {
  struct mem *self = (struct mem *)obj;
  free(self);
  return 0;
}

int mem_read(struct dicm_src *const src, void *buf, size_t size) {
  struct mem *self = (struct mem *)src;
  const ptrdiff_t diff = self->end - self->cur;
  assert(diff >= 0);
  if ((size_t)diff >= size) {
    memcpy(buf, self->cur, size);
    self->cur += size;
    return 0;
  }
  self->cur = self->end;
  return -1;
}

int mem_seek(struct dicm_src *const src, long offset, int whence) {
  struct mem *self = (struct mem *)src;
  const void *ptr = NULL;
  // SEEK_SET, SEEK_CUR, or SEEK_END:
  switch (whence) {
  case SEEK_SET:
    ptr = self->beg + offset;
    break;
  case SEEK_CUR:
    ptr = self->cur + offset;
    break;
  case SEEK_END:
    ptr = self->end + offset;
    break;
  }
  if (ptr == NULL || ptr < self->beg || ptr > self->end) {
    return -1;
  }
  self->cur = ptr;
  return 0;
}

int dicm_src_mem_create(struct dicm_src **pself, const void *ptr, size_t size) {
  assert(ptr);
  struct mem *self = (struct mem *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->super;
    self->super.vtable = &g_mem_vtable;
    self->cur = self->beg = ptr;
    self->end = ptr + size;
    return 0;
  }
  *pself = NULL;
  return -1;
}

static DICM_CHECK_RETURN int user_destroy(struct object *) DICM_NONNULL;
int user_destroy(struct object *obj) {
  struct dicm_src_user *self = (struct dicm_src_user *)obj;
  struct dicm_src_vtable *vtable = (struct dicm_src_vtable *)self->super.vtable;
  free(vtable);
  free(self);
  return 0;
}

int dicm_src_user_create(struct dicm_src **pself, void *data,
                         int (*fp_read)(struct dicm_src *const, void *, size_t),
                         int (*fp_seek)(struct dicm_src *const, long, int)) {
  struct dicm_src_user *self = (struct dicm_src_user *)malloc(sizeof(*self));
  if (self) {
    struct dicm_src_vtable *tmp =
        (struct dicm_src_vtable *)malloc(sizeof(*tmp));
    if (tmp) {
      struct dicm_src_vtable const obj = {
          /* obj interface */
          .obj = {.fp_destroy = user_destroy},
          /* src interface */
          .src = {.fp_read = fp_read, .fp_seek = fp_seek}};
      memcpy(tmp, &obj, sizeof(obj));
      *pself = &self->super;
      self->super.vtable = tmp;
      self->data = data;
      return 0;
    }
  }
  *pself = NULL;
  return -1;
}
