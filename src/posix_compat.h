/* https://developercommunity.visualstudio.com/t/in-c-header-systypesh-off-t-is-defined-as-32-bit-s/308714
 */
#ifdef _MSC_VER
static inline int fseeko(FILE *stream, int64_t offset, int origin) {
  return _fseeki64(stream, offset, origin);
}

static inline int64_t ftello(FILE *stream) { return _ftelli64(stream); }
#endif
