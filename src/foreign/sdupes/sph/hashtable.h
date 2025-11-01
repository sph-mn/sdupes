
#ifndef sph_hashtable_h_included
#define sph_hashtable_h_included

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#if (SIZE_MAX > 0xffffffffu)
#define sph_hashtable_calculate_size_extra(n) n = (n | (n >> 32))
#else
#define sph_hashtable_calculate_size_extra n
#endif

#define sph_hashtable_empty 0
#define sph_hashtable_full 1
#define sph_hashtable_hash_integer(key, hashtable_size) (key % hashtable_size)
#define sph_hashtable_equal_integer(key_a, key_b) (key_a == key_b)
#define sph_hashtable_declare_type(name, key_type, value_type, hashtable_hash, hashtable_equal, size_factor) \
  typedef struct { \
    size_t size; \
    size_t mask; \
    uint8_t* flags; \
    key_type* keys; \
    value_type* values; \
  } name##_t; \
  size_t name##_calculate_size(size_t n) { \
    n = (size_factor * n); \
    if (n < 2) { \
      n = 2; \
    }; \
    n = (n - 1); \
    n = (n | (n >> 1)); \
    n = (n | (n >> 2)); \
    n = (n | (n >> 4)); \
    n = (n | (n >> 8)); \
    n = (n | (n >> 16)); \
    sph_hashtable_calculate_size_extra(n); \
    return ((1 + n)); \
  } \
  uint8_t name##_new(size_t minimum_size, name##_t* out) { \
    name##_t a; \
    size_t n; \
    a.flags = 0; \
    a.keys = 0; \
    a.values = 0; \
    n = name##_calculate_size(minimum_size); \
    a.flags = calloc(n, 1); \
    if (!a.flags) { \
      return (1); \
    }; \
    a.keys = calloc(n, (sizeof(key_type))); \
    if (!a.keys) { \
      free((a.flags)); \
      return (1); \
    }; \
    a.values = malloc((n * sizeof(value_type))); \
    if (!a.values) { \
      free((a.flags)); \
      free((a.keys)); \
      return (1); \
    }; \
    a.size = n; \
    a.mask = (n - 1); \
    *out = a; \
    return (0); \
  } \
  void name##_clear(name##_t a) { memset((a.flags), 0, (a.size)); } \
  void name##_free(name##_t a) { \
    free((a.values)); \
    free((a.keys)); \
    free((a.flags)); \
  } \
  value_type* name##_get(name##_t a, key_type key) { \
    size_t table_size = a.size; \
    size_t mask = a.mask; \
    size_t index = hashtable_hash(key, table_size); \
    size_t steps = 0; \
    while ((steps < table_size)) { \
      if ((a.flags)[index] == sph_hashtable_empty) { \
        return (0); \
      }; \
      if (((a.flags)[index] == sph_hashtable_full) && hashtable_equal(((a.keys)[index]), key)) { \
        return ((&((a.values)[index]))); \
      }; \
      index = ((index + 1) & mask); \
      steps += 1; \
    }; \
    return (0); \
  } \
  value_type* name##_set(name##_t a, key_type key, value_type value) { \
    size_t table_size = a.size; \
    size_t mask = a.mask; \
    size_t index = hashtable_hash(key, table_size); \
    size_t steps = 0; \
    while ((steps < table_size)) { \
      if (((a.flags)[index] == sph_hashtable_full) && hashtable_equal(((a.keys)[index]), key)) { \
        return ((a.values + index)); \
      }; \
      if (!((a.flags)[index] == sph_hashtable_full)) { \
        (a.flags)[index] = sph_hashtable_full; \
        (a.keys)[index] = key; \
        (a.values)[index] = value; \
        return ((a.values + index)); \
      }; \
      index = ((index + 1) & mask); \
      steps += 1; \
    }; \
    return (0); \
  } \
  uint8_t name##_remove(name##_t a, key_type key) { \
    size_t table_size = a.size; \
    size_t mask = a.mask; \
    size_t index = hashtable_hash(key, table_size); \
    while (1) { \
      if (!((a.flags)[index] == sph_hashtable_full)) { \
        return (1); \
      }; \
      if (hashtable_equal(((a.keys)[index]), key)) { \
        break; \
      }; \
      index = ((index + 1) & mask); \
    }; \
    size_t hole_index = index; \
    size_t home_index; \
    size_t distance_hole; \
    size_t distance_index; \
    while (1) { \
      index = ((index + 1) & mask); \
      if (!((a.flags)[index] == sph_hashtable_full)) { \
        (a.flags)[hole_index] = sph_hashtable_empty; \
        return (0); \
      }; \
      home_index = hashtable_hash(((a.keys)[index]), table_size); \
      distance_hole = ((hole_index - home_index) & mask); \
      distance_index = ((index - home_index) & mask); \
      if (distance_hole <= distance_index) { \
        (a.keys)[hole_index] = (a.keys)[index]; \
        (a.values)[hole_index] = (a.values)[index]; \
        hole_index = index; \
      }; \
    }; \
    return (1); \
  }
#endif
