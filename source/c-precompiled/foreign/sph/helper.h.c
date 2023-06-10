
#ifndef sph_helper_h
#define sph_helper_h

/* depends on status.h */
#include <inttypes.h>
enum { sph_helper_status_id_memory };
#define sph_helper_status_group ((uint8_t*)("sph"))

/** add explicit type cast to prevent compiler warning */
#define sph_helper_malloc(size, result) sph_helper_primitive_malloc(size, ((void**)(result)))
#define sph_helper_malloc_string(size, result) sph_helper_primitive_malloc_string(size, ((uint8_t**)(result)))
#define sph_helper_calloc(size, result) sph_helper_primitive_calloc(size, ((void**)(result)))
#define sph_helper_realloc(size, result) sph_helper_primitive_realloc(size, ((void**)(result)))
uint8_t* sph_helper_status_description(status_t a);
uint8_t* sph_helper_status_name(status_t a);
status_t sph_helper_primitive_malloc(size_t size, void** result);
status_t sph_helper_primitive_malloc_string(size_t length, uint8_t** result);
status_t sph_helper_primitive_calloc(size_t size, void** result);
status_t sph_helper_primitive_realloc(size_t size, void** memory);
void sph_helper_display_bits_u8(uint8_t a);
void sph_helper_display_bits(void* a, size_t size);
#endif
