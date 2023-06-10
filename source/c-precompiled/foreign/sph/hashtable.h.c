
#ifndef sph_hashtable_h
#define sph_hashtable_h

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
/* a macro that defines hash-table data types for arbitrary key/value types,
with linear probing for collision resolve and hash and equal functions customisable
by defining macro variables and re-including the source.
prime numbers from https://planetmath.org/goodhashtableprimes */
uint32_t sph_hashtable_primes[] = { 53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457, 1610612741 };
uint32_t* sph_hashtable_primes_end = (sph_hashtable_primes + 25);
#endif

#define sph_hashtable_hash_integer(key, hashtable_size) (key % hashtable_size)
#define sph_hashtable_equal_integer(key_a, key_b) (key_a == key_b)
#define sph_hashtable_declare_type(name, key_type, value_type, hashtable_hash, hashtable_equal, size_factor) \
  typedef struct { \
    size_t size; \
    uint8_t* flags; \
    key_type* keys; \
    value_type* values; \
  } name##_t; \
  size_t name##_calculate_size(size_t min_size) { \
    min_size = (size_factor * min_size); \
    uint32_t* primes; \
    for (primes = sph_hashtable_primes; (primes <= sph_hashtable_primes_end); primes += 1) { \
      if (min_size <= *primes) { \
        return ((*primes)); \
      }; \
    }; \
    /* if no prime has been found, make size at least an odd number */ \
    return ((1 | min_size)); \
  } \
  uint8_t name##_new(size_t min_size, name##_t* result) { \
    uint8_t* flags; \
    key_type* keys; \
    value_type* values; \
    min_size = name##_calculate_size(min_size); \
    flags = calloc(min_size, 1); \
    if (!flags) { \
      return (1); \
    }; \
    keys = calloc(min_size, (sizeof(key_type))); \
    if (!keys) { \
      free(flags); \
      return (1); \
    }; \
    values = malloc((min_size * sizeof(value_type))); \
    if (!values) { \
      free(keys); \
      free(flags); \
      return (1); \
    }; \
    (*result).flags = flags; \
    (*result).keys = keys; \
    (*result).values = values; \
    (*result).size = min_size; \
    return (0); \
  } \
  void name##_free(name##_t a) { \
    free((a.values)); \
    free((a.keys)); \
    free((a.flags)); \
  } \
\
  /** returns the address of the value in the hash table, 0 if it was not found */ \
  value_type* name##_get(name##_t a, key_type key) { \
    size_t i; \
    size_t hash_i; \
    hash_i = hashtable_hash(key, (a.size)); \
    i = hash_i; \
    while ((i < a.size)) { \
      if ((a.flags)[i]) { \
        if (hashtable_equal(key, ((a.keys)[i]))) { \
          return ((i + a.values)); \
        }; \
      } else { \
        return (0); \
      }; \
      i += 1; \
    }; \
    /* wraps over */ \
    i = 0; \
    while ((i < hash_i)) { \
      if ((a.flags)[i]) { \
        if (hashtable_equal(key, ((a.keys)[i]))) { \
          return ((i + a.values)); \
        }; \
      } else { \
        return (0); \
      }; \
      i += 1; \
    }; \
    return (0); \
  } \
\
  /** returns the address of the added or already included value, 0 if there is no space left in the hash table */ \
  value_type* name##_set(name##_t a, key_type key, value_type value) { \
    size_t i; \
    size_t hash_i; \
    hash_i = hashtable_hash(key, (a.size)); \
    i = hash_i; \
    while ((i < a.size)) { \
      if ((a.flags)[i]) { \
        if (hashtable_equal(key, ((a.keys)[i]))) { \
          return ((i + a.values)); \
        } else { \
          i += 1; \
        }; \
      } else { \
        (a.flags)[i] = 1; \
        (a.keys)[i] = key; \
        (a.values)[i] = value; \
        return ((i + a.values)); \
      }; \
    }; \
    i = 0; \
    while ((i < hash_i)) { \
      if ((a.flags)[i]) { \
        if (hashtable_equal(key, ((a.keys)[i]))) { \
          return ((i + a.values)); \
        } else { \
          i += 1; \
        }; \
      } else { \
        (a.flags)[i] = 1; \
        (a.keys)[i] = key; \
        (a.values)[i] = value; \
        return ((i + a.values)); \
      }; \
    }; \
    return (0); \
  } \
\
  /** returns 0 if the element was removed, 1 if it was not found. \
         only needs to set flag to zero */ \
  uint8_t name##_remove(name##_t a, key_type key) { \
    value_type* value = name##_get(a, key); \
    if (value) { \
      (a.flags)[(value - a.values)] = 0; \
      return (0); \
    } else { \
      return (1); \
    }; \
  } \
  void name##_clear(name##_t a) { memset((a.flags), 0, (a.size)); }
