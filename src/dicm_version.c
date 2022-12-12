#include "dicm_private.h"

#include "dicm_configure.h"

const char *dicm_get_version_string(void) { return DICM_VERSION; }

void dicm_get_version_numbers(int *major, int *minor, int *patch)

{
  *major = DICM_VERSION_MAJOR;
  *minor = DICM_VERSION_MINOR;
  *patch = DICM_VERSION_PATCH;
}
