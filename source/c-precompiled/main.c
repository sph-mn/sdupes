#define _POSIX_C_SOURCE 201000
#define input_path_count_min 1024
#define input_path_count_max 0
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "./foreign/sph/imht-set.c"
#include "./foreign/sph/i-array.c"
#include "./foreign/sph/status.c"
#include "./foreign/sph/helper.c"
i_array_declare_type(i_array_b64, uint64_t);
uint8_t get_input_paths(i_array_b64* output) {
  i_array_declare(result, i_array_b64);
  char* line;
  char* line_copy;
  size_t line_size;
  line = 0;
  line_size = 0;
  if (i_array_allocate_i_array_b64(input_path_count_min, (&result))) {
    return (1);
  };
  while (!(-1 == getline((&line), (&line_size), stdin))) {
    if (!line_size) {
      continue;
    };
    if ((i_array_length(result) > i_array_max_length(result)) && i_array_resize_i_array_b64((&result), (2 * i_array_max_length(result)))) {
      return (1);
    };
    line_copy = malloc(line_size);
    if (!line_copy) {
      return (1);
    };
    memcpy(line_copy, line, line_size);
    i_array_add(result, ((uint64_t)(line_copy)));
  };
  *output = result;
  return (0);
}
int main() {
  i_array_declare(paths, i_array_b64);
  if (!(sizeof(uint64_t) == sizeof(char*))) {
    fprintf(stderr, "error: program assumes that pointers are 64 bit");
    return (1);
  };
  if (get_input_paths((&paths))) {
    fprintf(stderr, "error: memory allocation error while reading input paths");
    return (1);
  };
  while (i_array_in_range(paths)) {
    printf("%s", (i_array_get(paths)));
    i_array_forward(paths);
  };
  return (0);
}
