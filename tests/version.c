#include "dicm.h"
#include <stdlib.h> /* EXIT_SUCCESS */
int version(int argc, char *argv[]) {
  const char *vers = dicm_get_version_string();
  if (!vers) {
    return EXIT_FAILURE;
  }

  int major, minor, patch;
  dicm_get_version_numbers(&major, &minor, &patch);

  char buf[16];
  int n = sprintf(buf, "%d.%d.%d", major, minor, patch);
  if (n < 0) {
    return EXIT_FAILURE;
  }

  if (strcmp(vers, buf) != 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
