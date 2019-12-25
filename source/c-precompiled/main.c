/* error handling: message lines on standard error, ignore if possible, exit on memory error.
  ids are indexes in the paths array */
#define _POSIX_C_SOURCE 201000
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include "./foreign/murmur3.c"
#include "./foreign/sph/status.c"
#include "./foreign/sph/hashtable.c"
#include "./foreign/sph/i-array.c"
#include "./foreign/sph/helper.c"
#include "./foreign/sph/quicksort.c"
#define input_path_count_min 1024
#define input_path_count_max 0
#define part_checksum_page_count 1
#define flag_display_clusters 1
#define flag_null_delimiter 2
#define flag_exit 4
#define flag_sort_reverse 8
#define error(format, ...) fprintf(stderr, "error: %s:%d " format "\n", __func__, __LINE__, __VA_ARGS__)
#define memory_error \
  error("%s", "memory allocation failed"); \
  exit(1);
typedef struct {
  uint64_t a;
  uint64_t b;
} checksum_t;
typedef uint64_t id_t;
typedef struct {
  id_t id;
  uint64_t ctime;
} id_ctime_t;
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
uint8_t id_ctime_less_p(void* a, ssize_t b, ssize_t c) { return (((((id_ctime_t*)(a))[b]).ctime < (((id_ctime_t*)(a))[c]).ctime)); }
uint8_t id_ctime_greater_p(void* a, ssize_t b, ssize_t c) { return (((((id_ctime_t*)(a))[b]).ctime > (((id_ctime_t*)(a))[c]).ctime)); }
void id_ctime_swapper(void* a, ssize_t b, ssize_t c) {
  id_ctime_t d;
  d = ((id_ctime_t*)(a))[b];
  ((id_ctime_t*)(a))[b] = ((id_ctime_t*)(a))[c];
  ((id_ctime_t*)(a))[c] = d;
}
/** sort ids in-place via temporary array of pairs of id and ctime */
uint8_t sort_ids_by_ctime(ids_t ids, paths_t paths, uint8_t sort_descending) {
  int file;
  size_t i;
  size_t id_count;
  struct stat stat_info;
  uint8_t* path;
  id_ctime_t* ids_ctime;
  id_count = i_array_length(ids);
  ids_ctime = malloc((id_count * sizeof(id_ctime_t)));
  if (!ids_ctime) {
    memory_error;
  };
  for (i = 0; (i < id_count); i += 1) {
    path = i_array_get_at(paths, (i_array_get_at(ids, i)));
    file = open(path, O_RDONLY);
    if (file < 0) {
      error("couldnt open %s %s", (strerror(errno)), path);
      free(ids_ctime);
      return (1);
    };
    if (fstat(file, (&stat_info))) {
      error("couldnt stat %s %s", (strerror(errno)), path);
      close(file);
      free(ids_ctime);
      return (1);
    };
    close(file);
    (ids_ctime[i]).id = i_array_get_at(ids, i);
    (ids_ctime[i]).ctime = stat_info.st_ctime;
  };
  quicksort((sort_descending ? id_ctime_greater_p : id_ctime_less_p), id_ctime_swapper, ids_ctime, 0, (id_count - 1));
  for (i = 0; (i < id_count); i += 1) {
    (ids.start)[i] = (ids_ctime[i]).id;
  };
  free(ids_ctime);
  return (0);
}
void display_help() {
  printf("usage: sdupes\n");
  printf("description\n");
  printf(("  read file paths from standard input and display excess duplicate files, each set sorted by creation time ascending.\n"));
  printf(("  considers only regular files. files are duplicate if they have the same size, center portion and murmur3 hash\n"));
  printf("options\n");
  printf("  --help, -h  display this help text\n");
  printf("  --cluster, -c  display all duplicate paths, two newlines between each set\n");
  printf("  --null, -n for results: use the null byte as path delimiter, two null bytes between each set\n");
  printf("  --sort-reverse, -s  sort clusters by creation time descending\n");
}
uint8_t cli(int argc, char** argv) {
  int opt;
  struct option longopts[5] = { { "help", no_argument, 0, 'h' }, { "cluster", no_argument, 0, 'c' }, { "null", no_argument, 0, 'n' }, { "sort-reverse", no_argument, 0, 's' }, { 0, 0, 0, 0 } };
  uint8_t options = 0;
  while (!(-1 == (opt = getopt_long(argc, argv, "chns", longopts, 0)))) {
    if ('h' == opt) {
      display_help();
      options = (flag_exit | options);
      break;
    } else if ('c' == opt) {
      options = (flag_display_clusters | options);
    } else if ('n' == opt) {
      options = (flag_null_delimiter | options);
    } else if ('s' == opt) {
      options = (flag_sort_reverse | options);
    };
  };
  return (options);
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
  if (fstat(file, (&stat_info))) {
    error("couldnt stat %s %s", (strerror(errno)), path);
    close(file);
    return (1);
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
        close(file);
        return (0);
      };
    } else {
      part_start = 0;
      part_length = stat_info.st_size;
    };
  } else {
    result->a = 0;
    result->b = 0;
    close(file);
    return (0);
  };
  if (!page_size) {
    part_length = stat_info.st_size;
  };
  file_buffer = mmap(0, part_length, PROT_READ, MAP_SHARED, file, part_start);
  close(file);
  if (MAP_FAILED == file_buffer) {
    error("%s", (strerror(errno)));
    return (1);
  };
  MurmurHash3_x64_128(file_buffer, part_length, 0, temp);
  munmap(file_buffer, part_length);
  result->a = temp[0];
  result->b = temp[1];
  return (0);
}
paths_t get_input_paths() {
  i_array_declare(result, paths_t);
  char* line;
  char* line_copy;
  size_t line_size;
  ssize_t char_count;
  line = 0;
  line_size = 0;
  if (paths_new(input_path_count_min, (&result))) {
    memory_error;
  };
  char_count = getline((&line), (&line_size), stdin);
  while (!(-1 == char_count)) {
    if (!line_size) {
      continue;
    };
    if ((i_array_length(result) > i_array_max_length(result)) && paths_resize((&result), (2 * i_array_max_length(result)))) {
      memory_error;
    };
    /* getline always returns the delimiter if not end of input */
    if (char_count && ('\n' == line[(char_count - 1)])) {
      line[(char_count - 1)] = 0;
      line_size = (line_size - 1);
    };
    line_copy = malloc(line_size);
    if (!line_copy) {
      memory_error;
    };
    memcpy(line_copy, line, line_size);
    i_array_add(result, line_copy);
    char_count = getline((&line), (&line_size), stdin);
  };
  if (ENOMEM == errno) {
    memory_error;
  };
  free(line);
  return (result);
}
/** the result will only contain ids of regular files (no directories, symlinks or similar) */
hashtable_64_ids_t get_sizes(paths_t paths) {
  id_t* existing1;
  ids_t* existing2;
  hashtable_64_id_t ht1;
  hashtable_64_ids_t ht2;
  struct stat stat_info;
  i_array_declare(ids, ids_t);
  if (hashtable_64_id_new((i_array_length(paths)), (&ht1)) || hashtable_64_ids_new((i_array_length(paths)), (&ht2))) {
    memory_error;
  };
  while (i_array_in_range(paths)) {
    if (stat((i_array_get(paths)), (&stat_info))) {
      error("%s %s\n", (strerror(errno)), (i_array_get(paths)));
    };
    if (!S_ISREG((stat_info.st_mode))) {
      i_array_forward(paths);
      continue;
    };
    existing1 = hashtable_64_id_get(ht1, (stat_info.st_size));
    if (existing1) {
      existing2 = hashtable_64_ids_get(ht2, (stat_info.st_size));
      if (existing2) {
        if (i_array_length((*existing2)) == i_array_max_length((*existing2))) {
          if (ids_resize(existing2, (2 * i_array_max_length((*existing2))))) {
            memory_error;
          };
        };
        i_array_add((*existing2), (i_array_get_index(paths)));
      } else {
        if (ids_new(2, (&ids))) {
          memory_error;
        };
        i_array_add(ids, (*existing1));
        i_array_add(ids, (i_array_get_index(paths)));
        hashtable_64_ids_set(ht2, (stat_info.st_size), ids);
      };
    } else {
      hashtable_64_id_set(ht1, (stat_info.st_size), (i_array_get_index(paths)));
    };
    i_array_forward(paths);
  };
  hashtable_64_id_free(ht1);
  return (ht2);
}
/** assumes that all ids are for regular files */
hashtable_checksum_ids_t get_checksums(paths_t paths, ids_t ids, size_t center_page_count, size_t page_size) {
  checksum_t checksum;
  id_t* existing1;
  ids_t* existing2;
  hashtable_checksum_id_t ht1;
  hashtable_checksum_ids_t ht2;
  i_array_declare(value_ids, ids_t);
  if (hashtable_checksum_id_new((i_array_length(ids)), (&ht1)) || hashtable_checksum_ids_new((i_array_length(ids)), (&ht2))) {
    memory_error;
  };
  while (i_array_in_range(ids)) {
    if (get_checksum((i_array_get_at(paths, (i_array_get(ids)))), center_page_count, page_size, (&checksum))) {
      error("couldnt calculate checksum for %s", (i_array_get_at(paths, (i_array_get(ids)))));
    };
    existing1 = hashtable_checksum_id_get(ht1, checksum);
    if (existing1) {
      existing2 = hashtable_checksum_ids_get(ht2, checksum);
      if (existing2) {
        if (i_array_length((*existing2)) == i_array_max_length((*existing2))) {
          if (ids_resize(existing2, (2 * i_array_max_length((*existing2))))) {
            memory_error;
          };
        };
        i_array_add((*existing2), (i_array_get(ids)));
      } else {
        if (ids_new(2, (&value_ids))) {
          memory_error;
        };
        i_array_add(value_ids, (*existing1));
        i_array_add(value_ids, (i_array_get(ids)));
        hashtable_checksum_ids_set(ht2, checksum, value_ids);
      };
    } else {
      hashtable_checksum_id_set(ht1, checksum, (i_array_get(ids)));
    };
    i_array_forward(ids);
  };
  hashtable_checksum_id_free(ht1);
  return (ht2);
}
/** also frees hashtable and its values */
void display_result(paths_t paths, hashtable_checksum_ids_t ht, uint8_t cluster, uint8_t null, uint8_t sort_reverse) {
  size_t i;
  ids_t ids;
  uint8_t delimiter;
  delimiter = (null ? '0' : '\n');
  for (i = 0; (i < ht.size); i += 1) {
    if (!(ht.flags)[i]) {
      continue;
    };
    ids = (ht.values)[i];
    if (sort_ids_by_ctime(ids, paths, sort_reverse)) {
      continue;
    };
    if (cluster) {
      printf("%c", delimiter);
    } else {
      i_array_forward(ids);
    };
    while (i_array_in_range(ids)) {
      printf("%s%c", (i_array_get_at(paths, (i_array_get(ids)))), delimiter);
      i_array_forward(ids);
    };
    i_array_free(ids);
  };
  hashtable_checksum_ids_free(ht);
}
int main(int argc, char** argv) {
  hashtable_64_ids_t sizes_ht;
  hashtable_checksum_ids_t part_checksums_ht;
  size_t i;
  size_t j;
  uint8_t options;
  size_t page_size;
  i_array_declare(paths, paths_t);
  options = cli(argc, argv);
  if (flag_exit & options) {
    exit(0);
  };
  page_size = sysconf(_SC_PAGE_SIZE);
  paths = get_input_paths();
  sizes_ht = get_sizes(paths);
  for (i = 0; (i < sizes_ht.size); i += 1) {
    if (!(sizes_ht.flags)[i]) {
      continue;
    };
    part_checksums_ht = get_checksums(paths, ((sizes_ht.values)[i]), part_checksum_page_count, page_size);
    i_array_free(((sizes_ht.values)[i]));
    for (j = 0; (j < part_checksums_ht.size); j += 1) {
      if (!(part_checksums_ht.flags)[j]) {
        continue;
      };
      display_result(paths, (get_checksums(paths, ((part_checksums_ht.values)[j]), 0, 0)), (options & flag_display_clusters), (options & flag_null_delimiter), (options & flag_sort_reverse));
      i_array_free(((part_checksums_ht.values)[j]));
    };
    hashtable_checksum_ids_free(part_checksums_ht);
  };
  return (0);
}
