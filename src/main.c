// error handling: message lines on standard error. ignore files and continue if possible but exit on memory errors.
// ids are indices of the paths array.

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sdupes/sph/array.h>
#include <sdupes/sph/hashtable.h>
#include <sdupes/sph/set.h>
#include <sdupes/sph/queue.h>
#include <murmur3.c>
#include <pthread.h>

typedef struct {uint64_t data[6];} checksum_t;
typedef size_t id_t;
typedef struct {id_t id; time_t time;} id_time_t;
typedef struct {dev_t device; ino_t inode;} device_and_inode_t;
sph_array_declare_type(ids, id_t) // ids_t

typedef struct {
  atomic_int count;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} latch_t;

typedef struct get_ids_by_size_stat_t {
  dev_t st_dev;
  ino_t st_ino;
  off_t st_size;
  mode_t st_mode;
} get_ids_by_size_stat_t;

typedef struct get_ids_by_size_task_data_t {
  get_ids_by_size_stat_t* stats;
  id_t start;
  id_t end;
} get_ids_by_size_task_data_t;

typedef struct get_cluster_task_data_t {
  ids_t ids;
  uint8_t options;
  char delimiter;
} get_cluster_task_data_t;

struct sph_thread_pool_task_t;

typedef struct sph_thread_pool_task_t {
  sph_queue_node_t q;
  void (*f)(struct sph_thread_pool_task_t*);
  char** paths;
  latch_t* latch;
  union {
    get_ids_by_size_task_data_t get_ids_by_size;
    get_cluster_task_data_t get_cluster;
  } data;
} sph_thread_pool_task_t;

#define sph_thread_pool_task_t_defined
#include <sdupes/sph/thread-pool.c>
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
#define paths_per_thread 250

#define ids_add_with_resize(a, id) \
  if (a.used == a.size) { \
    status = ids_resize(&a, 2 * a.size); \
    if (status_is_failure) memory_error; \
  } \
  else {sph_array_add(a, id);}

sph_hashtable_declare_type(id_by_size, off_t, id_t, sph_hashtable_hash_integer, sph_hashtable_equal_integer, 2)
sph_hashtable_declare_type(ids_by_size, off_t, ids_t, sph_hashtable_hash_integer, sph_hashtable_equal_integer, 2)
sph_hashtable_declare_type(id_by_checksum, checksum_t, id_t, checksum_hash, checksum_equal, 2)
sph_hashtable_declare_type(ids_by_checksum, checksum_t, ids_t, checksum_hash, checksum_equal, 2)
device_and_inode_t device_and_inode_null = {0};
sph_set_declare_type(device_and_inode_set, device_and_inode_t, device_and_inode_hash, device_and_inode_equal, device_and_inode_null, 2)
sph_thread_pool_t thread_pool;

char* simple_basename(char* path) {
  char* slash_pointer = strrchr(path, '/');
  return((slash_pointer ? (1 + slash_pointer) : path));
}

uint8_t file_open(char* path, int* out) {
  *out = open(path, O_RDONLY);
  if (*out < 0) {
    display_error("could not open %s %s", strerror(errno), path);
    return(1);
  };
  return(0);
}

uint8_t file_stat(int file, char* path, struct stat* out) {
  if (fstat(file, out)) {
    display_error("could not stat %s %s", strerror(errno), path);
    return(1);
  };
  return(0);
}

uint8_t file_size(int file, char* path, off_t* out) {
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

void latch_init(latch_t* latch, int initial_count) {
  atomic_init(&latch->count, initial_count);
  pthread_mutex_init(&latch->mutex, 0);
  pthread_cond_init(&latch->cond, 0);
}

void latch_wait(latch_t* latch) {
  pthread_mutex_lock(&latch->mutex);
  while (atomic_load(&latch->count) > 0) {
    pthread_cond_wait(&latch->cond, &latch->mutex);
  }
  pthread_mutex_unlock(&latch->mutex);
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
  char* path;
  struct stat stat_info;
  ids_time = malloc(ids.used * sizeof(id_time_t));
  if (!ids_time) memory_error;
  for (size_t i = 0; i < ids.used; i += 1) {
    id = sph_array_get(ids, i);
    ids_time[i].id = id;
    path = paths[id];
    if (file_open(path, &file)) {
      ids_time[i] = id_time_null;
      continue;
    };
    if (file_stat(file, path, &stat_info)) ids_time[i] = id_time_null;
    else ids_time[i].time = stat_info.st_ctime;
    close(file);
  };
  if (64 > ids.used) {
    for (uint8_t i = 1; i < ids.used; i += 1) {
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
    qsort(ids_time, ids.used, sizeof(id_time_t), (sort_descending ? id_time_greater_p : id_time_less_p));
  }
  for (size_t i = 0; (i < ids.used); i += 1) {
    sph_array_get(ids, i) = ids_time[i].id;
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
    "  --version, -v  show the program version number\n");
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
    } else if ('0' == opt) options = (flag_null_delimiter | options);
    else if ('c' == opt) options = (flag_display_clusters | options);
    else if ('d' == opt) options = (flag_ignore_content | options);
    else if ('n' == opt) options = (flag_ignore_filenames | options);
    else if ('r' == opt) options = (flag_reverse | options);
    else if ('v' == opt) {
      printf("v1.6\n");
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
  size_t data_size = paths_data_size_min;
  size_t data_used = 0;
  size_t paths_size = paths_size_min;
  ssize_t read_size;
  char* data = malloc(paths_data_size_min);
  char* p;
  char* data_end;
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
  handle_error(read_size);
  if (!data_used) exit(0);
  p = data;
  data_end = data + data_used;
  while (p < data_end) {
    char* next_delim = memchr(p, delimiter, data_end - p);
    if (!next_delim) break;
    if (next_delim > p) {
      if (paths_size == *paths_used) {
        paths_size *= 2;
        *paths = realloc(*paths, paths_size * sizeof(char*));
        if (!*paths) memory_error;
      }
      *next_delim = 0;
      (*paths)[*paths_used] = p;
      (*paths_used) += 1;
    }
    p = next_delim + 1;
  }
  return(data);
}

void get_ids_by_size_thread(sph_thread_pool_task_t* task) {
  sph_thread_pool_task_t t = *task;
  struct stat stat_info;
  for (id_t i = t.data.get_ids_by_size.start; i < t.data.get_ids_by_size.end; i += 1) {
    if (lstat(t.paths[i], &stat_info)) {
      display_error("could not stat %s %s", strerror(errno), t.paths[i]);
      continue;
    };
    t.data.get_ids_by_size.stats[i].st_dev = stat_info.st_dev;
    t.data.get_ids_by_size.stats[i].st_ino = stat_info.st_ino;
    t.data.get_ids_by_size.stats[i].st_size = stat_info.st_size;
    t.data.get_ids_by_size.stats[i].st_mode = stat_info.st_mode;
  }
  if (1 == atomic_fetch_sub(&t.latch->count, 1)) {
    pthread_mutex_lock(&t.latch->mutex);
    pthread_cond_signal(&t.latch->cond);
    pthread_mutex_unlock(&t.latch->mutex);
  }
}

void get_ids_by_size_stat(char** paths, size_t paths_size, get_ids_by_size_stat_t* stats, size_t batch_count) {
  sph_thread_pool_task_t* tasks;
  latch_t latch;
  tasks = malloc(batch_count * sizeof(sph_thread_pool_task_t));
  if (!tasks) memory_error;
  latch_init(&latch, batch_count);
  for (id_t i = 0; i < batch_count; i += 1) {
    tasks[i].data.get_ids_by_size.stats = stats;
    tasks[i].data.get_ids_by_size.start = i * paths_per_thread;
    tasks[i].data.get_ids_by_size.end = tasks[i].data.get_ids_by_size.start + paths_per_thread;
    if (tasks[i].data.get_ids_by_size.end > paths_size) tasks[i].data.get_ids_by_size.end = paths_size;
    tasks[i].paths = paths;
    tasks[i].latch = &latch;
    tasks[i].f = get_ids_by_size_thread;
    sph_thread_pool_enqueue(&thread_pool, tasks + i);
  }
  latch_wait(&latch);
  free(tasks);
}

ids_by_size_t get_ids_by_size(char** paths, size_t paths_size, id_t* cluster_count) {
  // the result will only contain ids of regular files (no directories, symlinks, etc)
  status_declare;
  device_and_inode_set_t device_and_inode_set;
  device_and_inode_t device_and_inode;
  get_ids_by_size_stat_t* stats;
  ids_by_size_t ids_by_size;
  id_by_size_t id_by_size;
  id_t* id;
  ids_t* ids;
  ids_t new_ids;
  size_t batch_count;
  batch_count = (paths_size + paths_per_thread - 1) / paths_per_thread;
  if (1 > batch_count) batch_count = 1;
  stats = malloc(paths_size * sizeof(get_ids_by_size_stat_t));
  if (!stats) memory_error;
  get_ids_by_size_stat(paths, paths_size, stats, batch_count);
  if (id_by_size_new(paths_size, &id_by_size) || ids_by_size_new(paths_size, &ids_by_size) || device_and_inode_set_new(paths_size, &device_and_inode_set)) {
    memory_error;
  }
  *cluster_count = 0;
  for (id_t i = 0; i < paths_size; i += 1) {
    if (!S_ISREG((stats[i].st_mode))) continue;
    id = id_by_size_get(id_by_size, stats[i].st_size);
    if (!id) {
      id_by_size_set(id_by_size, stats[i].st_size, i);
      continue;
    }
    if (stats[*id].st_dev == stats[i].st_dev && stats[*id].st_ino == stats[i].st_ino) {
      continue;
    }
    ids = ids_by_size_get(ids_by_size, stats[i].st_size);
    if (ids) {
      device_and_inode.device = stats[i].st_dev;
      device_and_inode.inode = stats[i].st_ino;
      if (!device_and_inode_set_get(device_and_inode_set, device_and_inode)) {
        ids_add_with_resize((*ids), i);
        device_and_inode_set_add(&device_and_inode_set, device_and_inode);
      }
      continue;
    }
    status = ids_new(8, &new_ids);
    if (status_is_failure) memory_error;
    sph_array_add(new_ids, *id);
    sph_array_add(new_ids, i);
    ids_by_size_set(ids_by_size, stats[i].st_size, new_ids);
    device_and_inode.device = stats[*id].st_dev;
    device_and_inode.inode = stats[*id].st_ino;
    device_and_inode_set_add(&device_and_inode_set, device_and_inode);
    device_and_inode.device = stats[i].st_dev;
    device_and_inode.inode = stats[i].st_ino;
    device_and_inode_set_add(&device_and_inode_set, device_and_inode);
    *cluster_count += 1;
  }
  free(stats);
  id_by_size_free(id_by_size);
  device_and_inode_set_free(device_and_inode_set);
  return(ids_by_size);
}

uint8_t get_checksum(char* path, checksum_t* out) {
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
  status_declare;
  checksum_t checksum;
  id_t* checksum_id;
  ids_t* checksum_ids;
  id_t id;
  id_by_checksum_t id_by_checksum;
  ids_by_checksum_t ids_by_checksum;
  ids_t new_checksum_ids;
  if (id_by_checksum_new(ids.used, &id_by_checksum) || ids_by_checksum_new(ids.used, &ids_by_checksum)) {
    memory_error;
  };
  for (size_t i; i < ids.used; i += 1) {
    id = sph_array_get(ids, i);
    if (get_checksum(paths[id], &checksum)) display_error("could not calculate checksum for %s", paths[id]);
    checksum_id = id_by_checksum_get(id_by_checksum, checksum);
    if (checksum_id) {
      checksum_ids = ids_by_checksum_get(ids_by_checksum, checksum);
      if (checksum_ids) {
        ids_add_with_resize((*checksum_ids), id);
      }
      else {
        status = ids_new(4, &new_checksum_ids);
        if (status_is_failure) memory_error;
        sph_array_add(new_checksum_ids, (*checksum_id));
        sph_array_add(new_checksum_ids, id);
        ids_by_checksum_set(ids_by_checksum, checksum, new_checksum_ids);
      };
    }
    else id_by_checksum_set(id_by_checksum, checksum, id);
  }
  id_by_checksum_free(id_by_checksum);
  return(ids_by_checksum);
}

ids_t get_duplicates(char** paths, ids_t ids, uint8_t ignore_filenames, uint8_t ignore_content) {
  // return ids whose file name or file content is equal.
  // compare each with the first.
  status_declare;
  id_t id;
  int file;
  off_t size;
  uint8_t* content;
  uint8_t* first_content;
  char* first_name;
  char* name;
  char* path;
  sph_array_declare(duplicates, ids_t);
  if (!ids.used) return(duplicates);
  id = sph_array_get(ids, 0);
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
  status = ids_new(ids.used, &duplicates);
  if (status_is_failure) memory_error;
  sph_array_add(duplicates, id);
  for (id_t i = 1; i < ids.used; i += 1) {
    id = sph_array_get(ids, i);
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
        sph_array_add(duplicates, id);
      }
      munmap(content, size);
    } else {
      sph_array_add(duplicates, id);
    }
  }
  munmap(first_content, size);
  if (1 == duplicates.used) sph_array_remove(duplicates);
  return(duplicates);
}

pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

void display_duplicates(char** paths, ids_t ids, char delimiter, uint8_t display_cluster, uint8_t reverse) {
  // assumes that ids contains at least two entries
  static uint8_t stdout_empty = 1;
  pthread_mutex_lock(&stdout_mutex);
  if (1 < ids.size && sort_ids_by_ctime(ids, paths, reverse)) goto exit;
  id_t i;
  if (display_cluster) {
    if (stdout_empty) stdout_empty = 0;
    else putchar(delimiter);
  } else {
    i = 1;
  }
  do {
    printf("%s%c", paths[sph_array_get(ids, i)], delimiter);
    i += 1;
  } while (i < ids.used);
 exit:
  pthread_mutex_unlock(&stdout_mutex);
}

void get_cluster_thread(sph_thread_pool_task_t* task) {
  sph_thread_pool_task_t t = *task;
  ids_by_checksum_t ids_by_checksum;
  ids_t ids;
  ids_t duplicates;
  ids_by_checksum = get_ids_by_checksum(t.paths, t.data.get_cluster.ids);
  ids_free(&t.data.get_cluster.ids);
  for (id_t i = 0; (i < ids_by_checksum.size); i += 1) {
    if (!(ids_by_checksum.flags)[i]) continue;
    ids = (ids_by_checksum.values)[i];
    duplicates = get_duplicates(t.paths, ids, t.data.get_cluster.options & flag_ignore_filenames,
      t.data.get_cluster.options & flag_ignore_content);
    if (duplicates.data != ids.data) ids_free(&ids);
    if (1 < duplicates.used) {
      display_duplicates(t.paths, duplicates, t.data.get_cluster.delimiter,
        t.data.get_cluster.options & flag_display_clusters, t.data.get_cluster.options & flag_reverse);
    };
    ids_free(&duplicates);
  };
  ids_by_checksum_free(ids_by_checksum);
  if (1 == atomic_fetch_sub(&(t.latch->count), 1)) {
    pthread_mutex_lock(&(t.latch->mutex));
    pthread_cond_signal(&(t.latch->cond));
    pthread_mutex_unlock(&(t.latch->mutex));
  }
}

int main(int argc, char** argv) {
  char** paths;
  char* paths_data;
  id_t cluster_count;
  latch_t latch;
  size_t paths_size;
  size_t thread_count;
  sph_thread_pool_size_t cpu_count;
  sph_thread_pool_task_t* tasks;
  ids_by_size_t ids_by_size;
  ids_t ids;
  char delimiter;
  uint8_t options;
  options = cli(argc, argv);
  if (flag_exit & options) return(1);
  delimiter = ((options & flag_null_delimiter) ? 0 : '\n');
  paths_data = get_paths(delimiter, &paths, &paths_size);
  if (!paths_size) return(0);
  // initialize thread pool
  cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  thread_count = paths_size / paths_per_thread;
  if (1 > thread_count || 1 > cpu_count) thread_count = 1;
  else if (thread_count > cpu_count) thread_count = cpu_count;
  if (sph_thread_pool_new(thread_count, &thread_pool)) return(1);
  // start filtering
  ids_by_size = get_ids_by_size(paths, paths_size, &cluster_count);
  if (!cluster_count) return(0);
  tasks = malloc(cluster_count * sizeof(sph_thread_pool_task_t));
  if (!tasks) memory_error;
  latch_init(&latch, cluster_count);
  for (id_t i = 0; (i < ids_by_size.size); i += 1) {
    if (!ids_by_size.flags[i]) continue;
    ids = (ids_by_size.values)[i];
    cluster_count -= 1;
    tasks[cluster_count].data.get_cluster.ids = ids;
    tasks[cluster_count].data.get_cluster.options = options;
    tasks[cluster_count].data.get_cluster.delimiter = delimiter;
    tasks[cluster_count].paths = paths;
    tasks[cluster_count].latch = &latch;
    tasks[cluster_count].f = get_cluster_thread;
    sph_thread_pool_enqueue(&thread_pool, tasks + cluster_count);
  };
  latch_wait(&latch);
  free(tasks);
  ids_by_size_free(ids_by_size);
  free(paths_data);
  free(paths);
  sph_thread_pool_finish(&thread_pool, 0, 0);
  pthread_mutex_destroy(&stdout_mutex);
  return(0);
}
