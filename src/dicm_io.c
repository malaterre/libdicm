#include "dicm_io.h"

#include <assert.h>  /* assert */
#include <errno.h>   /* errno */
#include <stdbool.h> /* bool */
#include <stdlib.h>  /* malloc */

struct _stream {
  struct dicm_io io;
};

struct _file {
  struct dicm_io io;
  /* data */
  FILE *stream;
  const char *filename;
};

#if 0
// https://stackoverflow.com/questions/58670828/is-there-a-way-to-rewind-stdin-in-c
static bool is_stream_seekable(FILE* stream) {
  if (fseek(stdin, 0L, SEEK_SET)) {
    return false;
  }
  return true;
}
#endif

static DICM_CHECK_RETURN int _stream_destroy(void *self_) DICM_NONNULL;
static DICM_CHECK_RETURN int _stream_read(void *self_, void *buf,
                                          size_t size) DICM_NONNULL;
static DICM_CHECK_RETURN int _stream_write(void *self_, void const *buf,
                                           size_t size) DICM_NONNULL;

static DICM_CHECK_RETURN int _file_destroy(void *self_) DICM_NONNULL;
static DICM_CHECK_RETURN int _file_read(void *self_, void *buf,
                                        size_t size) DICM_NONNULL;
static DICM_CHECK_RETURN int _file_seek(void *self_, long offset,
                                        int whence) DICM_NONNULL;
static DICM_CHECK_RETURN int _file_write(void *self_, void const *buf,
                                         size_t size) DICM_NONNULL;

static struct io_vtable const g_vtable = {
    /* object interface */
    .object = {.fp_destroy = _file_destroy},
    /* io interface */
    .io = {
        .fp_read = _file_read, .fp_seek = _file_seek, .fp_write = _file_write}};

static int dicm_io_file_create(struct dicm_io **pself, const char *filename,
                               bool read_mode) {
  int errsv = 0;
  struct _file *self = (struct _file *)malloc(sizeof(*self));
  errsv = errno; /* ENOMEM */
  if (self) {
    *pself = &self->io;
    self->io.vtable = &g_vtable;
    FILE *stream = read_mode ? fopen(filename, "rb") : fopen(filename, "wb");
    errsv = errno;
    self->stream = stream;
    self->filename = filename;
    // TODO:
    // https://en.cppreference.com/w/c/io/setvbuf
    if (stream) {
      return 0;
    }
  }
  // log_errno(debug, errsv); // FIXME
  *pself = NULL;
  return errsv;
}

int dicm_input_file_create(struct dicm_io **pself, const char *filename) {
  return dicm_io_file_create(pself, filename, true);
}

int dicm_output_file_create(struct dicm_io **pself, const char *filename) {
  return dicm_io_file_create(pself, filename, false);
}

static struct io_vtable const g_in_vtable = {
    /* object interface */
    .object = {.fp_destroy = _stream_destroy},
    /* io interface */
    .io = {.fp_read = _stream_read
#if 0
		, .fp_seek = _file_seek, .fp_write = _file_write
#endif
    }};

static struct io_vtable const g_out_vtable = {
    /* object interface */
    .object = {.fp_destroy = _stream_destroy},
    /* io interface */
    .io = {
#if 0
        .fp_read = _file_read, .fp_seek = _file_seek,
#endif
        .fp_write = _stream_write}};

static int dicm_io_stream_create(struct dicm_io **pself, bool read_mode) {
  int errsv = 0;
  struct _file *self = (struct _file *)malloc(sizeof(*self));
  errsv = errno; /* ENOMEM */
  if (self) {
    *pself = &self->io;
    self->io.vtable = read_mode ? &g_in_vtable : &g_out_vtable;
    return 0;
  }
  *pself = NULL;
  return errsv;
}

int dicm_input_stream_create(struct dicm_io **pself) {
  return dicm_io_stream_create(pself, true);
}

int dicm_output_stream_create(struct dicm_io **pself) {
  return dicm_io_stream_create(pself, false);
}

int _stream_destroy(void *const self_) {
  int errsv = 0;
  struct _stream *self = (struct _stream *)self_;
  free(self);
  return errsv;
}

int _file_destroy(void *const self_) {
  int errsv = 0;
  struct _file *self = (struct _file *)self_;
  if (self->filename) {
    /* it is an error only if the stream was already opened */
    if (self->stream && fclose(self->stream)) {
      /* some failure */
      errsv = errno;
      assert(errno < 0);
    }
  }
  free(self);
  return errsv;
}

int _stream_read(void *const self_, void *buf, size_t size) {
  struct _file *self = (struct _file *)self_;
  FILE *stream = stdin;
  const size_t read = fread(buf, 1, size, stream);
  /* fread() does not distinguish between end-of-file and error, and callers
   * must use feof(3) and ferror(3) to determine which occurred. */
  if (read != size) {
    const int eof = feof(stream);
    const int error = ferror(stream);
    if (error)
      return -1;
    assert(eof && read == 0);
  }
  const int ret = (int)read;
  assert(ret >= 0);
  return ret;
}

int _file_read(void *const self_, void *buf, size_t size) {
  struct _file *self = (struct _file *)self_;
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

int _file_seek(void *const self_, long offset, int whence) {
  assert(0);
  return 1;
}

int _stream_write(void *const self_, void const *buf, size_t size) {
  struct _stream *self = (struct _stream *)self_;
  FILE *stream = stdout;
  const size_t write = fwrite(buf, 1, size, stream);
  if (write != size) {
    // TODO
    assert(0);
    return -1;
  }
  const int ret = (int)write;
  assert(ret >= 0);
  return ret;
}

int _file_write(void *const self_, void const *buf, size_t size) {
  struct _file *self = (struct _file *)self_;
  const size_t write = fwrite(buf, 1, size, self->stream);
  if (write != size) {
    // TODO
    assert(0);
    return -1;
  }
  const int ret = (int)write;
  assert(ret >= 0);
  return ret;
}

int dicm_delete(void *self_) {
  struct dicm_io *self = (struct dicm_io *)self_;
  return object_destroy(self);
}

static struct io_vtable g_stream_vtable = {
    /* object interface */
    .object = {.fp_destroy = _stream_destroy},
    /* io interface */
    .io = {.fp_read = NULL, .fp_seek = NULL, .fp_write = NULL}};

int dicm_create(struct dicm_io **pself,
                int (*fp_read)(void *const, void *, size_t),
                int (*fp_seek)(void *const, long, int), /* can be null */
                int (*fp_write)(void *const, const void *, size_t)) {
  struct _stream *self = (struct _stream *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->io;
    g_stream_vtable.io.fp_read = fp_read;
    self->io.vtable = &g_stream_vtable;
    return 0;
  }
  *pself = NULL;
  return -1;
}
