#define _POSIX_C_SOURCE 201000
#define input_path_count_min 1024
#define input_path_count_max 0
#define part_checksum_page_count 2
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "./foreign/murmur3.c"
#include "./foreign/sph/status.c"
#include "./foreign/sph/hashtable.c"
#include "./foreign/sph/i-array.c"
#include "./foreign/sph/helper.c"
typedef struct {
  uint64_t a;
  uint64_t b;
} checksum_t;
typedef uint64_t id_t;
i_array_declare_type(ids, id_t);
i_array_declare_type(paths, uint8_t*);
hashtable_declare_type(hashtable_64_id, uint64_t, id_t);
hashtable_declare_type(hashtable_64_ids, uint64_t, ids_t);
#undef hashtable_hash
#undef hashtable_equal
#define hashtable_hash(key, hashtable) (key.a % hashtable.size)
#define hashtable_equal(key_a, key_b) ((key_a.a == key_b.a) && (key_a.b == key_b.b))
hashtable_declare_type(hashtable_checksum_id, checksum_t, id_t);
hashtable_declare_type(hashtable_checksum_ids, checksum_t, ids_t);
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
    /* getline always returns the delimiter if not end of input */
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
#define error(format, ...) fprintf(stderr, "error: %s:%d " format "\n", __func__, __LINE__, __VA_ARGS__)
#define memory_error \
  error("%s", "memory allocation failed"); \
  exit(1);
/** the result will only contain ids of regular files (no directories, symlinks or similar) */
hashtable_64_ids_t get_sizes(paths_t paths) {
  int status;
  id_t* existing;
  ids_t* second_existing;
  struct stat stat_info;
  hashtable_64_id_t first_ht;
  hashtable_64_ids_t second_ht;
  i_array_declare(ids, ids_t);
  if (hashtable_64_id_new((i_array_length(paths)), (&first_ht)) || hashtable_64_ids_new((i_array_length(paths)), (&second_ht))) {
    memory_error;
  };
  while (i_array_in_range(paths)) {
    status = stat((i_array_get(paths)), (&stat_info));
    if (status) {
      error("%s %s\n", (strerror(errno)), (i_array_get(paths)));
    };
    if (!S_ISREG((stat_info.st_mode))) {
      i_array_forward(paths);
      continue;
    };
    existing = hashtable_64_id_get(first_ht, (stat_info.st_size));
    if (existing) {
      second_existing = hashtable_64_ids_get(second_ht, (stat_info.st_size));
      if (second_existing) {
        /* resize value if necessary */
        if (i_array_length((*second_existing)) == i_array_max_length((*second_existing))) {
          if (ids_resize(second_existing, (2 * i_array_max_length((*second_existing))))) {
            memory_error;
          };
        };
        i_array_add((*second_existing), (i_array_get_index(paths)));
      } else {
        if (ids_new(4, (&ids))) {
          memory_error;
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
  hashtable_64_id_destroy(first_ht);
  return (second_ht);
}
/** center-page-count and page-size are optional and can be zero.
   if center-page-count is not zero, only a centered part of the file is checksummed.
   in this case page-size must also be non-zero */
uint8_t get_checksum(uint8_t* path, size_t center_page_count, size_t page_size, checksum_t* result) {
  int file;
  uint8_t* file_buffer;
  struct stat stat_info;
  uint64_t temp[2];
  size_t part_start;
  size_t part_length;
  file = open(path, O_RDONLY);
  if (file < 0) {
    error("couldnt open %s %s", (strerror(errno)), path);
    return (1);
  };
  if (fstat(file, (&stat_info)) < 0) {
    error("couldnt stat %s %s", (strerror(errno)), path);
    goto error;
  };
  if (stat_info.st_size) {
    if (center_page_count) {
      if (stat_info.st_size > (2 * page_size * part_checksum_page_count)) {
        part_start = (stat_info.st_size / page_size);
        part_start = ((3 > part_start) ? page_size : (page_size * (part_start / 2)));
        part_length = (page_size * part_checksum_page_count);
        if (part_length > stat_info.st_size) {
          part_length = (stat_info.st_size - part_start);
        };
      } else {
        result->a = 0;
        result->b = 0;
        goto exit;
      };
    } else {
      part_start = 0;
      part_length = stat_info.st_size;
    };
  } else {
    result->a = 0;
    result->b = 0;
    goto exit;
  };
  file_buffer = mmap(0, part_length, PROT_READ, MAP_SHARED, file, part_start);
  if (0 > file_buffer) {
    error("%s %s\n", (strerror(errno)), path);
    goto error;
  };
  MurmurHash3_x64_128(file_buffer, part_length, 0, temp);
  result->a = temp[0];
  result->b = temp[1];
  munmap(file_buffer, part_length);
exit:
  close(file);
  return (0);
error:
  close(file);
  return (1);
}
/** assumes that all ids are for regular files */
hashtable_checksum_ids_t get_checksums(paths_t paths, ids_t ids, size_t center_page_count, size_t page_size) {
  checksum_t checksum;
  id_t* existing;
  hashtable_checksum_id_t first_ht;
  ids_t* second_existing;
  hashtable_checksum_ids_t second_ht;
  i_array_declare(value_ids, ids_t);
  if (hashtable_checksum_id_new((i_array_length(ids)), (&first_ht)) || hashtable_checksum_ids_new((i_array_length(ids)), (&second_ht))) {
    memory_error;
  };
  while (i_array_in_range(ids)) {
    if (get_checksum((i_array_get_at(paths, (i_array_get(ids)))), center_page_count, page_size, (&checksum))) {
      error("%s", "couldnt calculate checksum for ", (i_array_get_at(paths, (i_array_get(ids)))));
    };
    existing = hashtable_checksum_id_get(first_ht, checksum);
    if (existing) {
      second_existing = hashtable_checksum_ids_get(second_ht, checksum);
      if (second_existing) {
        /* resize value if necessary */
        if (i_array_length((*second_existing)) == i_array_max_length((*second_existing))) {
          if (ids_resize(second_existing, (2 * i_array_max_length((*second_existing))))) {
            memory_error;
          };
        };
        i_array_add((*second_existing), (i_array_get(ids)));
      } else {
        if (ids_new(4, (&value_ids))) {
          memory_error;
        };
        i_array_add(value_ids, (*existing));
        i_array_add(value_ids, (i_array_get(ids)));
        hashtable_checksum_ids_set(second_ht, checksum, value_ids);
      };
    } else {
      hashtable_checksum_id_set(first_ht, checksum, (i_array_get(ids)));
    };
    i_array_forward(ids);
  };
  hashtable_checksum_id_destroy(first_ht);
  return (second_ht);
}
int main() {
  hashtable_64_ids_t sizes_ht;
  hashtable_checksum_ids_t checksums_ht;
  hashtable_checksum_ids_t part_checksums_ht;
  size_t sizes_i;
  size_t part_checksums_i;
  size_t checksums_i;
  size_t page_size;
  page_size = sysconf(_SC_PAGE_SIZE);
  i_array_declare(paths, paths_t);
  if (get_input_paths((&paths))) {
    memory_error;
  };
  /* cluster by size */
  sizes_ht = get_sizes(paths);
  /* hashtable value i-arrays are not explicity freed */
  for (sizes_i = 0; (sizes_i < sizes_ht.size); sizes_i += 1) {
    if ((sizes_ht.flags)[sizes_i]) {
      /* cluster by center portion checksum */
      part_checksums_ht = get_checksums(paths, ((sizes_ht.values)[sizes_i]), part_checksum_page_count, page_size);
      for (part_checksums_i = 0; (part_checksums_i < part_checksums_ht.size); part_checksums_i += 1) {
        if ((part_checksums_ht.flags)[part_checksums_i]) {
          /* cluster by complete file checksum */
          checksums_ht = get_checksums(paths, ((part_checksums_ht.values)[part_checksums_i]), 0, 0);
          /* display found duplicates */
          for (checksums_i = 0; (checksums_i < checksums_ht.size); checksums_i += 1) {
            if ((checksums_ht.flags)[checksums_i]) {
              i_array_forward(((checksums_ht.values)[checksums_i]));
              while (i_array_in_range(((checksums_ht.values)[checksums_i]))) {
                printf("%s\n", (i_array_get_at(paths, (i_array_get(((checksums_ht.values)[checksums_i]))))));
                i_array_forward(((checksums_ht.values)[checksums_i]));
              };
            };
          };
          hashtable_checksum_ids_destroy(checksums_ht);
        };
      };
      hashtable_checksum_ids_destroy(part_checksums_ht);
    };
  };
  return (0);
}
