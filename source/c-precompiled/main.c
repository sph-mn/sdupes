#define _POSIX_C_SOURCE 201000
#define input_path_count_min 1024
#define input_path_count_max 0
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "./foreign/murmur3.c"
#include "./foreign/sph/hashtable.c"
#include "./foreign/sph/i-array.c"
#include "./foreign/sph/status.c"
#include "./foreign/sph/helper.c"
i_array_declare_type(ids, uint64_t);
i_array_declare_type(paths, uint8_t*);
hashtable_declare_type(hashtable_64_id, uint64_t, uint64_t);
hashtable_declare_type(hashtable_64_ids, uint64_t, ids_t);
uint8_t get_input_paths(paths_t* output) {
  i_array_declare(result, paths_t);
  char* line;
  char* line_copy;
  size_t line_size;
  ssize_t char_count;
  line = 0;
  line_size = 0;
  if (paths_new(input_path_count_min, (&result))) {
    return (1);
  };
  char_count = getline((&line), (&line_size), stdin);
  while (!(-1 == char_count)) {
    if (!line_size) {
      continue;
    };
    if ((i_array_length(result) > i_array_max_length(result)) && paths_resize((&result), (2 * i_array_max_length(result)))) {
      return (1);
    };
    /* getline returns the delimiter if present */
    if (char_count && ('\n' == line[(char_count - 1)])) {
      line[(char_count - 1)] = 0;
      line_size = (line_size - 1);
    };
    line_copy = malloc(line_size);
    if (!line_copy) {
      return (1);
    };
    memcpy(line_copy, line, line_size);
    i_array_add(result, line_copy);
    char_count = getline((&line), (&line_size), stdin);
  };
  *output = result;
  return (0);
}
hashtable_64_ids_t get_sizes(paths_t paths) {
  int error;
  uint64_t* existing;
  ids_t* second_existing;
  struct stat stat_info;
  hashtable_64_id_t first_ht;
  hashtable_64_ids_t second_ht;
  i_array_declare(ids, ids_t);
  hashtable_64_id_new((i_array_length(paths)), (&first_ht));
  hashtable_64_ids_new((i_array_length(paths)), (&second_ht));
  while (i_array_in_range(paths)) {
    error = stat((i_array_get(paths)), (&stat_info));
    if (error) {
      fprintf(stderr, "error: get-sizes: %s %s\n", (strerror(errno)), (i_array_get(paths)));
    };
    if (!S_ISREG((stat_info.st_mode))) {
      i_array_forward(paths);
      continue;
    };
    existing = hashtable_64_id_get(first_ht, (stat_info.st_size));
    if (existing) {
      second_existing = hashtable_64_ids_get(second_ht, (stat_info.st_size));
      if (second_existing) {
        printf("%lu");
        /* resize value if necessary */
        if (i_array_length((*second_existing)) == i_array_max_length((*second_existing))) {
          if (ids_resize(second_existing, (2 * i_array_max_length((*second_existing))))) {
            exit(1);
            fprintf(stderr, "error: get-sizes: memory allocation failed\n");
          };
        };
        i_array_add((*second_existing), (i_array_get_index(paths)));
      } else {
        if (ids_new(4, (&ids))) {
          exit(1);
          fprintf(stderr, "error: get-sizes: memory allocation failed\n");
        };
        i_array_add(ids, (*existing));
        i_array_add(ids, (i_array_get_index(paths)));
        hashtable_64_ids_set(second_ht, (stat_info.st_size), ids);
      };
    } else {
      hashtable_64_id_set(first_ht, (stat_info.st_size), (i_array_get_index(paths)));
    };
    i_array_forward(paths);
  };
  size_t i;
  for (i = 0; (i < second_ht.size); i += 1) {
    if ((second_ht.flags)[i]) {
      printf("duplicate set\n");
      while (i_array_in_range(((second_ht.values)[i]))) {
        printf("  %s\n", (i_array_get_at(paths, (i_array_get(((second_ht.values)[i]))))));
        i_array_forward(((second_ht.values)[i]));
      };
    };
  };
  return (second_ht);
}
int main() {
  i_array_declare(paths, paths_t);
  if (!(sizeof(uint64_t) == sizeof(char*))) {
    fprintf(stderr, "error: program assumes that pointers are 64 bit");
    return (1);
  };
  if (get_input_paths((&paths))) {
    fprintf(stderr, "error: memory allocation error while reading input paths");
    return (1);
  };
  uint64_t output[2];
  MurmurHash3_x64_128("test", 4, 48, output);
  printf("%llx %llx\n", (output[0]), (output[1]));
  return (0);
}
