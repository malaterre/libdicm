#ifndef DICM_H
#define DICM_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint32_t */

/**
 * @defgroup export Export Definitions
 * @{
 */

/** The public API declaration. */

#ifdef __GNUC__
#define DICM_DECLARE(type) __attribute__((visibility("default"))) type
#define DICM_CHECK_RETURN __attribute__((__warn_unused_result__))
#define DICM_NONNULL __attribute__((nonnull))
#else
#define DICM_DECLARE(type) __declspec(dllexport) type
#define DICM_CHECK_RETURN
#define DICM_NONNULL
#endif

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup version Version Information
 * @{
 */

/**
 * Get the library version as a string.
 *
 * @returns The function returns the pointer to a static string of the form
 * @c "X.Y.Z", where @c X is the major version number, @c Y is a minor version
 * number, and @c Z is the patch version number.
 */

DICM_DECLARE(const char *)
dicm_get_version_string(void);

/**
 * Get the library version numbers.
 *
 * @param[out]      major   Major version number.
 * @param[out]      minor   Minor version number.
 * @param[out]      patch   Patch version number.
 */

DICM_DECLARE(void)
dicm_get_version(int *major, int *minor, int *patch) DICM_NONNULL;

/** @} */

/**
 * @defgroup common Common object
 * @{
 */

/**
 * Delete an object
 *
 * @param[in]      self   An object.
 */
DICM_DECLARE(int)
dicm_delete(void *self) DICM_NONNULL;

/** @} */

/**
 * @defgroup log Logging
 * @{
 */
DICM_DECLARE(void)
dicm_configure_log_msg(void (*fp_msg)(int, const char *)) DICM_NONNULL;

/** @} */

/**
 * @defgroup io Input/Output
 * @{
 */
struct dicm_io;

/* buffer */
DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_input_buffer_create(struct dicm_io **pself, const void *ptr,
                         size_t len) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_output_buffer_create(struct dicm_io **pself, void *ptr,
                          size_t len) DICM_NONNULL;

/* stream */
DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_input_stream_create(struct dicm_io **pself) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_output_stream_create(struct dicm_io **pself) DICM_NONNULL;

/* file */
DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_input_file_create(struct dicm_io **pself,
                       const char *filename) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_output_file_create(struct dicm_io **pself,
                        const char *filename) DICM_NONNULL;

/* user-defined */
DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_create(struct dicm_io **pself, int (*fp_read)(void *const, void *, size_t),
            int (*fp_seek)(void *const, long, int), /* can be null */
            int (*fp_write)(void *const, const void *, size_t));
/** @} */

/**
 * @defgroup parser Parser
 * @{
 */

/** Event types. */
enum dicm_event_type {
  /* negative value are reserved for errors */
  /** A STREAM-START event. */
  DICM_STREAM_START_EVENT = 0,
  /** A STREAM-END event. */
  DICM_STREAM_END_EVENT,

  /** A DATASET-START event. */
  DICM_DATASET_START_EVENT,
  /** A DATASET-END event. */
  DICM_DATASET_END_EVENT,

  /** A ELEMENT-KEY event. */
  DICM_ELEMENT_KEY_EVENT,
  /** A FRAGMENT event. */
  DICM_FRAGMENT_EVENT,
  /** A ELEMENT-VALUE event (valid for both element key or fragment). */
  DICM_ELEMENT_VALUE_EVENT,

  /** A ITEM-START event. */
  DICM_ITEM_START_EVENT,
  /** A ITEM-END event. */
  DICM_ITEM_END_EVENT,

  /** A SEQUENCE-START event. */
  DICM_SEQUENCE_START_EVENT,
  /** A SEQUENCE-END event. */
  DICM_SEQUENCE_END_EVENT,
};

struct dicm_parser;

struct dicm_key {
  uint32_t tag;
  uint32_t vr;
};

/**
 * Create a parser
 *
 * This function creates a new parser object.  An application is responsible
 * for destroying the object using the dicm_delete() function.
 *
 * @param[out]      parser  An empty parser object.
 *
 * @returns @c 0 if the function succeeded, @c -1 on error.
 */
DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_parser_create(struct dicm_parser **pself) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_parser_set_input(struct dicm_parser *self,
                      struct dicm_io *io) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_parser_next_event(struct dicm_parser *self) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_parser_get_key(struct dicm_parser *self,
                    struct dicm_key *key) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_parser_get_value_length(struct dicm_parser *self,
                             size_t *len) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_parser_read_value(struct dicm_parser *self, void *ptr,
                       size_t len) DICM_NONNULL;

/** @} */

/**
 * @defgroup emitter Emitter
 * @{
 */

struct dicm_emitter;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_emitter_create(struct dicm_emitter **pself) DICM_NONNULL;

DICM_CHECK_RETURN
DICM_DECLARE(int)
dicm_emitter_set_output(struct dicm_emitter *self,
                        struct dicm_io *io) DICM_NONNULL;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DICM_H */
