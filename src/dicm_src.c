#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "dicm_src.h"

#include "posix_compat.h"

#include <stdio.h>  /* FILE */
#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

struct file {
  struct dicm_src super;
  /* data */
  FILE *stream;
};

static DICM_CHECK_RETURN int file_destroy(struct object *) DICM_NONNULL();
static DICM_CHECK_RETURN int64_t file_read(struct dicm_src *, void *, size_t)
    DICM_NONNULL();
static DICM_CHECK_RETURN int64_t file_seek(struct dicm_src *, int64_t, int)
    DICM_NONNULL();

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

/* $ man 2 read
 * [...]
 * On Linux, read() (and similar system calls) will transfer at most 0x7ffff000
 * (2,147,479,552) bytes, returning the number of bytes actually transferred.
 * (This is true on both 32-bit and 64-bit systems.)
 */

#define DICM_SIZE_MAX 0x7ffff000

int64_t file_read(struct dicm_src *const src, void *buf, size_t size) {
  assert(size <= DICM_SIZE_MAX);
  struct file *self = (struct file *)src;
  assert(is_aligned(buf, 4));
  const size_t read = fread(buf, 1, size, self->stream);
  /* fread() does not distinguish between end-of-file and error, and callers
   * must use feof(3) and ferror(3) to determine which occurred. */
  if (read != size) {
    const int error = ferror(self->stream);
    if (error)
      return -1;
  }
  const int64_t ret = (int64_t)read;
  assert(ret >= 0);
  return ret;
}

/* $ man 2 lseek */
int64_t file_seek(struct dicm_src *const src, int64_t offset, int whence) {
  struct file *self = (struct file *)src;
  const int ret = fseeko(self->stream, offset, whence);
  if (ret < 0)
    return ret;
  return ftello(self->stream);
}

#if 0
/* https://stackoverflow.com/questions/2082743/c-equivalent-to-fstreams-peek */
static inline int fpeek(FILE *stream) {
  int c;

  c = fgetc(stream);
  ungetc(c, stream);

  return c;
}
#endif

/* https://stackoverflow.com/questions/58670828/is-there-a-way-to-rewind-stdin-in-c
 */
static inline bool is_stream_seekable(FILE *stream) {
  if (fseek(stream, 0L, SEEK_SET)) {
    return false;
  }
  return true;
}

static inline bool is_stream_valid(FILE *stream) {
  const int eof = feof(stream);
  const int err = ferror(stream);
  return eof == 0 && err == 0;
}

int dicm_src_file_create(struct dicm_src **pself, FILE *stream) {
  assert(stream);
  assert(is_stream_valid(stream));
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
  const char *cur;
  const char *beg;
  const char *end;
};

static DICM_CHECK_RETURN int mem_destroy(struct object *) DICM_NONNULL();
static DICM_CHECK_RETURN int64_t mem_read(struct dicm_src *, void *, size_t)
    DICM_NONNULL();
static DICM_CHECK_RETURN int64_t mem_seek(struct dicm_src *, int64_t, int)
    DICM_NONNULL();

static struct dicm_src_vtable const g_mem_vtable = {
    .obj = {.fp_destroy = mem_destroy},
    .src = {.fp_read = mem_read, .fp_seek = mem_seek}};

int mem_destroy(struct object *obj) {
  struct mem *self = (struct mem *)obj;
  free(self);
  return 0;
}

int64_t mem_read(struct dicm_src *const src, void *buf, size_t size) {
  struct mem *self = (struct mem *)src;
  assert(is_aligned(buf, 4));
  const ptrdiff_t diff = self->end - self->cur;
  assert(diff >= 0);
  if ((size_t)diff >= size) {
    memcpy(buf, self->cur, size);
    self->cur += size;
    return size;
  }
  self->cur = self->end;
  return -1;
}

int64_t mem_seek(struct dicm_src *const src, int64_t offset, int whence) {
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
  return self->cur - self->beg;
}

int dicm_src_mem_create(struct dicm_src **pself, const void *ptr, size_t size) {
  assert(ptr);
  struct mem *self = (struct mem *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->super;
    self->super.vtable = &g_mem_vtable;
    self->cur = self->beg = ptr;
    self->end = (char *)ptr + size;
    return 0;
  }
  *pself = NULL;
  return -1;
}

static DICM_CHECK_RETURN int user_destroy(struct object *) DICM_NONNULL();
int user_destroy(struct object *obj) {
  struct dicm_src_user *self = (struct dicm_src_user *)obj;
  struct dicm_src_vtable *vtable = (struct dicm_src_vtable *)self->super.vtable;
  free(vtable);
  free(self);
  return 0;
}

static int
src_user_create(struct dicm_src **pself, void *data,
                int64_t (*fp_read)(struct dicm_src *, void *, size_t),
                int64_t (*fp_seek)(struct dicm_src *, int64_t, int)) {
  struct dicm_src_user *self = (struct dicm_src_user *)malloc(sizeof(*self));
  if (self) {
    struct dicm_src_vtable *tmp =
        (struct dicm_src_vtable *)malloc(sizeof(*tmp));
    if (tmp) {
      struct dicm_src_vtable const obj = {/* obj interface */
                                          .obj = {.fp_destroy = user_destroy},
                                          /* src interface */
                                          .src = {
                                              .fp_read = fp_read,
                                              .fp_seek = fp_seek,
                                          }};
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

int dicm_src_stream_create(struct dicm_src **pself, void *data,
                           int64_t (*fp_read)(struct dicm_src *, void *,
                                              size_t),
                           int64_t (*fp_seek)(struct dicm_src *, int64_t,
                                              int)) {
  return src_user_create(pself, data, fp_read, fp_seek);
}
