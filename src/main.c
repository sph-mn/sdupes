// error handling: message lines on standard error. ignore files and continue if possible but exit on memory errors.
// ids are indices of the paths array.

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <foreign/murmur3.c>
#include <foreign/sph-sc-lib/array4.h>
#include <foreign/sph-sc-lib/hashtable.h>
#include <foreign/sph-sc-lib/set.h>
#define paths_size_min 8192
#define path_size_min 512
#define paths_data_size_min paths_size_min * path_size_min
#define checksum_portion_size 16384
#define flag_display_clusters 1
#define flag_null_delimiter 2
#define flag_exit 4
#define flag_reverse 8
#define flag_ignore_filenames 16
#define flag_ignore_content 32
#define display_error(format, ...) fprintf(stderr, "error: %s:%d " format "\n", __func__, __LINE__, __VA_ARGS__)
#define memory_error do{display_error("%s", "memory allocation failed");exit(1);} while (0)
#define handle_error(a) if (0 > a) do {display_error("%s", strerror(errno));exit(1);} while (0)
#define checksum_hash(key, size) ((key.data)[1] % size)
#define checksum_equal(key_a, key_b) !memcmp((key_a.data), (key_b.data), 48)
#define device_and_inode_hash(key, size) (key.inode % size)
#define device_and_inode_equal(key_a, key_b) ((key_a.inode == key_b.inode) && (key_a.device == key_b.device))

#define ids_add_with_resize(a, id)                                      \
  if ((array4_size(a) == array4_max_size(a)) && ids_resize(&a, (2 * array4_max_size(a)))) { \
    memory_error; \
  } else { \
    array4_add(a, id); \
  }

typedef struct {uint64_t data[6];} checksum_t;
typedef size_t id_t;
typedef struct {id_t id; time_t time;} id_time_t;
typedef struct {dev_t device; ino_t inode;} device_and_inode_t;
array4_declare_type(ids, id_t); // ids_t
sph_hashtable_declare_type(id_by_size, off_t, id_t, sph_hashtable_hash_integer, sph_hashtable_equal_integer, 2);
sph_hashtable_declare_type(ids_by_size, off_t, ids_t, sph_hashtable_hash_integer, sph_hashtable_equal_integer, 2);
sph_hashtable_declare_type(id_by_checksum, checksum_t, id_t, checksum_hash, checksum_equal, 2);
sph_hashtable_declare_type(ids_by_checksum, checksum_t, ids_t, checksum_hash, checksum_equal, 2);
device_and_inode_t device_and_inode_null = {0};
sph_set_declare_type_nonull(device_and_inode_set, device_and_inode_t, device_and_inode_hash, device_and_inode_equal, device_and_inode_null, 2);

uint8_t* simple_basename(uint8_t* path) {
  uint8_t* slash_pointer = strrchr(path, '/');
  return((slash_pointer ? (1 + slash_pointer) : path));
}

uint8_t file_open(uint8_t* path, int* out) {
  *out = open(path, O_RDONLY);
  if (*out < 0) {
    display_error("could not open %s %s", strerror(errno), path);
    return(1);
  };
  return(0);
}

uint8_t file_stat(int file, uint8_t* path, struct stat* out) {
  if (fstat(file, out)) {
    display_error("could not stat %s %s", strerror(errno), path);
    return(1);
  };
  return(0);
}

uint8_t file_size(int file, uint8_t* path, off_t* out) {
  struct stat stat_info;
  if (file_stat(file, path, &stat_info)) return(1);
  *out = stat_info.st_size;
  return(0);
}

uint8_t file_mmap(int file, off_t size, uint8_t** out) {
  *out = mmap(0, size, PROT_READ, MAP_SHARED, file, 0);
  if (MAP_FAILED == *out) {
    perror(0);
    return(1);
  };
  return(0);
}

int id_time_less_p(const void* a, const void* b) {
  const id_time_t* aa = a;
  const id_time_t* bb = b;
  return(aa->time == bb->time
    ? (aa->id < bb->id ? -1 : aa->id > bb->id)
    : (aa->time < bb->time ? -1 : 1));
}

int id_time_greater_p(const void* a, const void* b) {return id_time_less_p(b, a);}

uint8_t sort_ids_by_ctime(ids_t ids, char** paths, uint8_t sort_descending) {
  // sort ids in-place via temporary array of pairs of id and ctime
  id_t id;
  id_time_t* ids_time;
  id_time_t id_time;
  id_time_t id_time_null = {0};
  int file;
  uint8_t* path;
  struct stat stat_info;
  ids_time = malloc(array4_size(ids) * sizeof(id_time_t));
  if (!ids_time) memory_error;
  for (size_t i = 0; i < array4_size(ids); i += 1) {
    id = array4_get_at(ids, i);
    path = paths[id];
    if (file_open(path, &file)) continue;
    if (file_stat(file, path, &stat_info)) ids_time[i] = id_time_null;
    else {
      ids_time[i].id = id;
      ids_time[i].time = stat_info.st_ctime;
    }
    close(file);
  };
  if (64 > array4_size(ids)) {
    for (uint8_t i = 1; i < array4_size(ids); i += 1) {
      id_time = ids_time[i];
      int8_t j = i - 1;
      while (j >= 0 && (sort_descending
        ? (ids_time[j].time == id_time.time ? ids_time[j].id < id_time.id : ids_time[j].time < id_time.time)
        : (ids_time[j].time == id_time.time ? ids_time[j].id > id_time.id : ids_time[j].time > id_time.time))) {
        ids_time[j + 1] = ids_time[j];
        j = j - 1;
      }
      ids_time[j + 1] = id_time;
    }
  }
  else {
    qsort(ids_time, array4_size(ids), sizeof(id_time_t), (sort_descending ? id_time_greater_p : id_time_less_p));
  }
  for (size_t i = 0; (i < array4_size(ids)); i += 1) {
    array4_get_at(ids, i) = ids_time[i].id;
  };
  free(ids_time);
  return(0);
}

void display_help() {
  printf("usage: sdupes\n"
    "description\n"
    "  read file paths from standard input and display paths of excess duplicate files sorted by creation time ascending.\n"
    "  considers only regular files with differing device and inode. files are duplicate if all of the following properties match:\n"
    "  * size\n"
    "  * murmur3 hashes of start, middle, and end portions\n"
    "  * name or content\n"
    "options\n"
    "  --help, -h  display this help text\n"
    "  --cluster, -c  display all paths with duplicates. two newlines between sets\n"
    "  --ignore-name, -n  do not consider file names\n"
    "  --ignore-content, -d  do not consider the full file content\n"
    "  --null, -0  use a null byte to delimit paths. two null bytes between sets\n"
    "  --reverse, -r  sort clusters by creation time descending\n"
    "  --version, -v  show the running program version number\n");
}

uint8_t cli(int argc, char** argv) {
  int opt;
  uint8_t options;
  struct option longopts[8] = {{"cluster", no_argument, 0, 'c'}, {"help", no_argument, 0, 'h'}, {"ignore-content", no_argument, 0, 'd'},
    {"ignore-name", no_argument, 0, 'n'}, {"null", no_argument, 0, '0'}, {"reverse", no_argument, 0, 'r'},
    {"version", no_argument, 0, 'v'}, {0}};
  options = 0;
  while (!(-1 == (opt = getopt_long(argc, argv, "0cdhnrv", longopts, 0)))) {
    if ('h' == opt) {
      display_help();
      options = (flag_exit | options);
      break;
    } else if ('0' == opt) {
      options = (flag_null_delimiter | options);
    } else if ('c' == opt) {
      options = (flag_display_clusters | options);
    } else if ('d' == opt) {
      options = (flag_ignore_content | options);
    } else if ('n' == opt) {
      options = (flag_ignore_filenames | options);
    } else if ('r' == opt) {
      options = (flag_reverse | options);
    } if ('v' == opt) {
      printf("v1.5\n");
      options = (flag_exit | options);
      break;
    };
  };
  return(options);
}

char* get_paths(char delimiter, char*** paths, size_t* paths_used) {
  // read delimiter separated paths from standard input.
  // all paths must end with the delimiter.
  // data will contain the full string read from standard input with newlines replaced by null bytes,
  // while paths contains pointers to the beginning of each path in data.
  size_t data_index = 0;
  size_t data_size = paths_data_size_min;
  size_t data_used = 0;
  size_t paths_size = paths_size_min;
  ssize_t read_size;
  char* data = malloc(paths_data_size_min);
  if (!data) memory_error;
  *paths = malloc(paths_size_min * sizeof(char*));
  if (!*paths) memory_error;
  *paths_used = 0;
  while (0 < (read_size = read(0, data + data_used, paths_data_size_min))) {
    data_used += read_size;
    if (data_size < data_used + paths_data_size_min) {
      data_size *= 2;
      data = realloc(data, data_size);
      if (!data) memory_error;
    }
  }
  for (size_t i = 0; i < data_used; i += 1) {
    if (delimiter == data[i] && i > data_index) {
      if (paths_size == *paths_used) {
        paths_size *= 2;
        *paths = realloc(*paths, paths_size * sizeof(char*));
        if (!*paths) memory_error;
      }
      data[i] = 0;
      (*paths)[*paths_used] = data + data_index;
      data_index = i + 1;
      *paths_used += 1;
    }
  }
  handle_error(read_size);
  if (!data_used) exit(0);
  return(data);
}

ids_by_size_t get_duplicated_ids_by_size(char** paths, size_t paths_size) {
  // the result will only contain ids of regular files (no directories, symlinks, etc)
  id_by_size_t id_by_size;
  id_t* id;
  ids_by_size_t ids_by_size;
  ids_t* ids;
  ids_t new_ids;
  device_and_inode_t device_and_inode;
  device_and_inode_set_t device_and_inode_set;
  struct stat stat_info;
  if (id_by_size_new(paths_size, &id_by_size) || ids_by_size_new(paths_size, &ids_by_size) || device_and_inode_set_new(paths_size, &device_and_inode_set)) {
    memory_error;
  };
  for (id_t i = 0; i < paths_size; i += 1) {
    if (lstat(paths[i], &stat_info)) {
      display_error("could not lstat %s %s", strerror(errno), paths[i]);
      continue;
    };
    if (!S_ISREG((stat_info.st_mode))) continue;
    device_and_inode.device = stat_info.st_dev;
    device_and_inode.inode = stat_info.st_ino;
    if (device_and_inode_set_get(device_and_inode_set, device_and_inode)) {
      continue;
    } else {
      device_and_inode_set_add(device_and_inode_set, device_and_inode);
    };
    id = id_by_size_get(id_by_size, (stat_info.st_size));
    if (!id) {
      id_by_size_set(id_by_size, (stat_info.st_size), i);
      continue;
    };
    ids = ids_by_size_get(ids_by_size, (stat_info.st_size));
    if (ids) {
      ids_add_with_resize((*ids), i);
      continue;
    };
    if (ids_new(8, &new_ids)) {
      memory_error;
    };
    array4_add(new_ids, (*id));
    array4_add(new_ids, i);
    ids_by_size_set(ids_by_size, (stat_info.st_size), new_ids);
  };
  id_by_size_free(id_by_size);
  device_and_inode_set_free(device_and_inode_set);
  return(ids_by_size);
}

uint8_t get_checksum(uint8_t* path, checksum_t* out) {
  uint8_t* buffer;
  int file;
  struct stat stat_info;
  checksum_t null = { 0 };
  *out = null;
  if (file_open(path, &file)) return(1);
  if (file_stat(file, path, &stat_info)) {
    close(file);
    return(1);
  };
  if (!stat_info.st_size) {
    close(file);
    return(0);
  };
  buffer = mmap(0, (stat_info.st_size), PROT_READ, MAP_SHARED, file, 0);
  close(file);
  if (MAP_FAILED == buffer) {
    perror(0);
    return(1);
  };
  if (stat_info.st_size < (2 * 3 * checksum_portion_size)) {
    MurmurHash3_x64_128(buffer, (stat_info.st_size), 0, (out->data));
  } else {
    MurmurHash3_x64_128(buffer, checksum_portion_size, 0, (out->data));
    MurmurHash3_x64_128((((stat_info.st_size / 2) - (checksum_portion_size / 2)) + buffer), checksum_portion_size, 0, (2 + out->data));
    MurmurHash3_x64_128(((stat_info.st_size - checksum_portion_size) + buffer), checksum_portion_size, 0, (4 + out->data));
  };
  munmap(buffer, (stat_info.st_size));
  return(0);
}

ids_by_checksum_t get_ids_by_checksum(char** paths, ids_t ids) {
  // assumes that all ids are of regular files
  checksum_t checksum;
  id_t* checksum_id;
  ids_t* checksum_ids;
  id_t id;
  id_by_checksum_t id_by_checksum;
  ids_by_checksum_t ids_by_checksum;
  ids_t new_checksum_ids;
  if (id_by_checksum_new((array4_size(ids)), &id_by_checksum) || ids_by_checksum_new((array4_size(ids)), &ids_by_checksum)) {
    memory_error;
  };
  while (array4_in_range(ids)) {
    id = array4_get(ids);
    if (get_checksum(paths[id], &checksum)) {
      display_error("could not calculate checksum for %s", paths[id]);
    };
    checksum_id = id_by_checksum_get(id_by_checksum, checksum);
    if (checksum_id) {
      checksum_ids = ids_by_checksum_get(ids_by_checksum, checksum);
      if (checksum_ids) {
        ids_add_with_resize((*checksum_ids), id);
      } else {
        if (ids_new(4, &new_checksum_ids)) {
          memory_error;
        };
        array4_add(new_checksum_ids, (*checksum_id));
        array4_add(new_checksum_ids, id);
        ids_by_checksum_set(ids_by_checksum, checksum, new_checksum_ids);
      };
    } else {
      id_by_checksum_set(id_by_checksum, checksum, id);
    };
    array4_forward(ids);
  };
  id_by_checksum_free(id_by_checksum);
  return(ids_by_checksum);
}

ids_t get_duplicates(char** paths, ids_t ids, uint8_t ignore_filenames, uint8_t ignore_content) {
  // return ids whose file name or file content is equal
  uint8_t* content;
  int file;
  uint8_t* first_content;
  uint8_t* first_name;
  id_t id;
  uint8_t* name;
  uint8_t* path;
  off_t size;
  array4_declare(duplicates, ids_t);
  if (!array4_in_range(ids)) return(duplicates);
  id = array4_get(ids);
  path = paths[id];
  first_name = simple_basename(path);
  if (file_open(path, &file)) return(duplicates);
  if (file_size(file, path, &size)) {
    close(file);
    return(duplicates);
  };
  if (!size) {
    close(file);
    return(ids);
  };
  if (file_mmap(file, size, &first_content)) {
    close(file);
    return(duplicates);
  };
  close(file);
  if (ids_new(array4_size(ids), &duplicates)) memory_error;
  array4_add(duplicates, id);
  for (array4_forward(ids); array4_in_range(ids); array4_forward(ids)) {
    id = array4_get(ids);
    path = paths[id];
    name = simple_basename(path);
    if (ignore_filenames && !ignore_content || !ignore_filenames && strcmp(first_name, name)) {
      if (ignore_content || file_open(path, &file)) continue;
      if (file_mmap(file, size, &content)) {
        close(file);
        continue;
      };
      close(file);
      if (!memcmp(first_content, content, size)) {
        array4_add(duplicates, id);
      };
      munmap(content, size);
    } else {
      array4_add(duplicates, id);
    };
  }
  munmap(first_content, size);
  if (1 == array4_size(duplicates)) {
    array4_remove(duplicates);
  };
  return(duplicates);
}

void display_duplicates(char** paths, ids_t ids, uint8_t delimiter, id_t cluster_count, uint8_t display_cluster, uint8_t reverse) {
  // assumes that ids contains at least two entries
  if (1 < ids.size && sort_ids_by_ctime(ids, paths, reverse)) return;
  if (display_cluster) {
    if (cluster_count) putchar(delimiter);
  } else array4_forward(ids);
  do {
    printf("%s%c", paths[array4_get(ids)], delimiter);
    array4_forward(ids);
  } while (array4_in_range(ids));
}

int main(int argc, char** argv) {
  uint8_t delimiter;
  ids_t duplicates;
  ids_by_checksum_t ids_by_checksum;
  ids_by_size_t ids_by_size;
  ids_t ids;
  uint8_t options;
  id_t cluster_count;
  char** paths;
  char* paths_data = 0;
  size_t paths_size;
  options = cli(argc, argv);
  if (flag_exit & options) return(1);
  delimiter = ((options & flag_null_delimiter) ? 0 : '\n');
  paths_data = get_paths(delimiter, &paths, &paths_size);
  ids_by_size = get_duplicated_ids_by_size(paths, paths_size);
  cluster_count = 0;
  for (size_t i = 0; (i < ids_by_size.size); i += 1) {
    if (!ids_by_size.flags[i]) continue;
    ids = (ids_by_size.values)[i];
    ids_by_checksum = get_ids_by_checksum(paths, ids);
    array4_free(ids);
    for (size_t j = 0; (j < ids_by_checksum.size); j += 1) {
      if (!(ids_by_checksum.flags)[j]) continue;
      ids = (ids_by_checksum.values)[j];
      duplicates = get_duplicates(paths, ids, options & flag_ignore_filenames, options & flag_ignore_content);
      if (!(duplicates.data == ids.data)) {
        array4_free(ids);
      };
      if (1 < array4_size(duplicates)) {
        display_duplicates(paths, duplicates, delimiter, cluster_count, options & flag_display_clusters, options & flag_reverse);
        cluster_count += 1;
      };
      array4_free(duplicates);
    };
    ids_by_checksum_free(ids_by_checksum);
  };
  ids_by_size_free(ids_by_size);
  free(paths_data);
  free(paths);
  return(0);
}
