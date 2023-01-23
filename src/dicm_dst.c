#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "dicm_dst.h"

#include <stdio.h>  /* FILE */
#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

struct file {
  struct dicm_dst super;
  /* data */
  FILE *stream;
};

static DICM_CHECK_RETURN int file_destroy(struct object *) DICM_NONNULL;
static DICM_CHECK_RETURN int64_t file_write(struct dicm_dst *, const void *,
                                            size_t) DICM_NONNULL;
static DICM_CHECK_RETURN int64_t file_seek(struct dicm_dst *, int64_t,
                                           int) DICM_NONNULL;

static struct dicm_dst_vtable const g_file_vtable = {
    .obj = {.fp_destroy = file_destroy},
    .dst = {.fp_write = file_write, .fp_seek = file_seek}};

static struct dicm_dst_vtable const g_stdstream_vtable = {
    .obj = {.fp_destroy = file_destroy},
    .dst = {.fp_write = file_write, .fp_seek = NULL}};

int file_destroy(struct object *obj) {
  struct file *self = (struct file *)obj;
  free(self);
  return 0;
}

int64_t file_write(struct dicm_dst *const dst, const void *buf, size_t size) {
  struct file *self = (struct file *)dst;
  const size_t write = fwrite(buf, 1, size, self->stream);
  if (write != size) {
    const int eof = feof(self->stream);
    const int error = ferror(self->stream);
    if (error)
      return -1;
    assert(eof && write == 0);
  }
  const int64_t ret = (int64_t)write;
  assert(ret >= 0);
  return ret;
}

int64_t file_seek(struct dicm_dst *const dst, int64_t offset, int whence) {
  struct file *self = (struct file *)dst;
  const off_t ret = fseeko(self->stream, offset, whence);
  if (ret < 0)
    return ret;
  return ftello(self->stream);
}

// https://stackoverflow.com/questions/58670828/is-there-a-way-to-rewind-stdin-in-c
static inline bool is_stream_seekable(FILE *stream) {
  if (fseek(stream, 0L, SEEK_SET)) {
    return false;
  }
  return true;
}

int dicm_dst_file_create(struct dicm_dst **pself, FILE *stream) {
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
  struct dicm_dst super;
  /* data */
  void *cur;
  void *beg;
  void *end;
};

static DICM_CHECK_RETURN int mem_destroy(struct object *) DICM_NONNULL;
static DICM_CHECK_RETURN int64_t mem_write(struct dicm_dst *const, const void *,
                                           size_t) DICM_NONNULL;
static DICM_CHECK_RETURN int64_t mem_seek(struct dicm_dst *const, int64_t,
                                          int) DICM_NONNULL;

static struct dicm_dst_vtable const g_mem_vtable = {
    .obj = {.fp_destroy = mem_destroy},
    .dst = {.fp_write = mem_write, .fp_seek = mem_seek}};

int mem_destroy(struct object *obj) {
  struct mem *self = (struct mem *)obj;
  free(self);
  return 0;
}

int64_t mem_write(struct dicm_dst *const dst, const void *buf, size_t size) {
  struct mem *self = (struct mem *)dst;
  const ptrdiff_t diff = self->end - self->cur;
  assert(diff >= 0);
  if ((size_t)diff >= size) {
    memcpy(self->cur, buf, size);
    self->cur += size;
    return (int64_t)size;
  }
  self->cur = self->end;
  return -1;
}

int64_t mem_seek(struct dicm_dst *const dst, int64_t offset, int whence) {
  struct mem *self = (struct mem *)dst;
  void *ptr = NULL;
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
  return self->cur - self->beg;
}

int dicm_dst_mem_create(struct dicm_dst **pself, void *ptr, size_t size) {
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
  struct dicm_dst_user *self = (struct dicm_dst_user *)obj;
  struct dicm_dst_vtable *vtable = (struct dicm_dst_vtable *)self->super.vtable;
  free(vtable);
  free(self);
  return 0;
}

int dicm_dst_user_create(struct dicm_dst **pself, void *data,
                         int64_t (*fp_write)(struct dicm_dst *const,
                                             const void *, size_t),
                         int64_t (*fp_seek)(struct dicm_dst *const, int64_t,
                                            int)) {
  struct dicm_dst_user *self = (struct dicm_dst_user *)malloc(sizeof(*self));
  if (self) {
    struct dicm_dst_vtable *tmp =
        (struct dicm_dst_vtable *)malloc(sizeof(*tmp));
    if (tmp) {
      struct dicm_dst_vtable const obj = {
          /* obj interface */
          .obj = {.fp_destroy = user_destroy},
          /* dst interface */
          .dst = {.fp_write = fp_write, .fp_seek = fp_seek}};
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
