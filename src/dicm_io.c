#include "dicm_io.h"

#include "dicm_log.h"

#include <assert.h>  /* assert */
#include <errno.h>   /* errno */
#include <stdbool.h> /* bool */
#include <stdio.h>   /* FILE */
#include <stdlib.h>  /* malloc */
#include <string.h>  /* memcpy */

struct _stdstream {
  struct dicm_io io;
};

struct _file {
  struct dicm_io io;
  /* data */
  FILE *stream;
  //  const char *filename;
};

// https://stackoverflow.com/questions/58670828/is-there-a-way-to-rewind-stdin-in-c
static inline bool is_stream_seekable(FILE *stream) {
  if (fseek(stream, 0L, SEEK_SET)) {
    return false;
  }
  return true;
}

static DICM_CHECK_RETURN int _stdstream_destroy(void *self_) DICM_NONNULL;
static DICM_CHECK_RETURN int _stdstream_read(void *self_, void *buf,
                                             size_t size) DICM_NONNULL;
static DICM_CHECK_RETURN int _stdstream_write(void *self_, void const *buf,
                                              size_t size) DICM_NONNULL;

static DICM_CHECK_RETURN int _file_destroy(void *self_) DICM_NONNULL;
static DICM_CHECK_RETURN int _file_read(void *self_, void *buf,
                                        size_t size) DICM_NONNULL;
static DICM_CHECK_RETURN int _file_seek(void *self_, long offset,
                                        int whence) DICM_NONNULL;
static DICM_CHECK_RETURN int _file_write(void *self_, void const *buf,
                                         size_t size) DICM_NONNULL;

static struct io_vtable const g_stdin_vtable = {
    /* object interface */
    .object = {.fp_destroy = _stdstream_destroy},
    /* io interface */
    .io = {.fp_read = _stdstream_read}};

static struct io_vtable const g_stdout_vtable = {
    /* object interface */
    .object = {.fp_destroy = _stdstream_destroy},
    /* io interface */
    .io = {.fp_write = _stdstream_write}};

static struct io_vtable const g_file_vtable = {
    /* object interface */
    .object = {.fp_destroy = _file_destroy},
    /* io interface */
    .io = {
        .fp_read = _file_read, .fp_seek = _file_seek, .fp_write = _file_write}};

int _stdstream_read(void *const self_, void *buf, size_t size) {
  struct _stdstream *self = (struct _stdstream *)self_;
  FILE *stream = stdin;
  assert(!is_stream_seekable(stream));
  const size_t read = fread(buf, 1, size, stream);
  /* fread() does not distinguish between end-of-file and error, and callers
   * must use feof(3) and ferror(3) to determine which occurred. */
  if (read != size) {
    const int eof = feof(stream);
    const int error = ferror(stream);
    if (error) {
      return -1;
    }
    assert(eof && read == 0);
  }
  const int ret = (int)read;
  assert(ret >= 0);
  return ret;
}

int _stdstream_write(void *const self_, void const *buf, size_t size) {
  struct _stdstream *self = (struct _stdstream *)self_;
  FILE *stream = stdout;
  assert(!is_stream_seekable(stream));
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

static int dicm_io_stream_create(struct dicm_io **pself, bool read_mode) {
  int errsv = 0;
  struct _stdstream *self = (struct _stdstream *)malloc(sizeof(*self));
  errsv = errno; /* ENOMEM */
  if (self) {
    *pself = &self->io;
    self->io.vtable = read_mode ? &g_stdin_vtable : &g_stdout_vtable;
    return 0;
  }
  *pself = NULL;
  assert(errsv > 0);
  return -errsv;
}

static int dicm_io_file_create(struct dicm_io **pself, const char *filename,
                               bool read_mode) {
  if (!filename) {
    _log_msg(LOG_ERROR, "Invalid filename");
    *pself = NULL;
    return -1;
  }
  int errsv = 0;
  struct _file *self = (struct _file *)malloc(sizeof(*self));
  errsv = errno; /* ENOMEM */
  if (self) {
    *pself = &self->io;
    self->io.vtable = &g_file_vtable;
    FILE *stream = read_mode ? fopen(filename, "rb") : fopen(filename, "wb");
    errsv = errno;
    self->stream = stream;
    // TODO:
    // https://en.cppreference.com/w/c/io/setvbuf
    if (stream) {
      return 0;
    }
    // else
  }
  _log_msg(LOG_ERROR, "Failed to open %s, errno is: %d", filename, errsv);
  *pself = NULL;
  assert(errsv > 0);
  return -errsv;
}

int dicm_input_file_create(struct dicm_io **pself, const char *filename) {
  return dicm_io_file_create(pself, filename, true);
}

int dicm_output_file_create(struct dicm_io **pself, const char *filename) {
  return dicm_io_file_create(pself, filename, false);
}

int dicm_input_stream_create(struct dicm_io **pself) {
  return dicm_io_stream_create(pself, true);
}

int dicm_output_stream_create(struct dicm_io **pself) {
  return dicm_io_stream_create(pself, false);
}

int _stdstream_destroy(void *const self_) {
  struct _stdstream *self = (struct _stdstream *)self_;
  free(self);
  return 0;
}

int _file_destroy(void *const self_) {
  int errsv = 0;
  struct _file *self = (struct _file *)self_;
  /* it is an error only if the stream was already opened */
  if (self->stream && fclose(self->stream)) {
    /* some failure */
    errsv = errno;
    assert(errsv > 0);
  }
  free(self);
  return -errsv;
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

struct _stream {
  struct dicm_io io;
  void *data;
};

int _stream_destroy(void *const self_) {
  struct _stream *self = (struct _stream *)self_;
  free(self->io.vtable);
  free(self);
  return 0;
}

int dicm_create(struct dicm_io **pself,
                int (*fp_read)(void *const, void *, size_t),
                int (*fp_seek)(void *const, long, int), /* can be null */
                int (*fp_write)(void *const, const void *, size_t)) {
  struct _stream *self = (struct _stream *)malloc(sizeof(*self));
  if (self) {
    *pself = &self->io;
    struct io_vtable *tmp = (struct io_vtable *)malloc(sizeof(*tmp));
    struct io_vtable obj = {
        /* object interface */
        .object = {.fp_destroy = _stream_destroy},
        /* io interface */
        .io = {.fp_read = fp_read, .fp_seek = fp_seek, .fp_write = fp_write}};
    memcpy(tmp, &obj, sizeof(obj));
    self->io.vtable = tmp;
    return 0;
  }
  *pself = NULL;
  return -1;
}
