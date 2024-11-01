
#ifndef sph_thread_pool_h
#define sph_thread_pool_h

#include <pthread.h>
/* thread-pool that uses pthread condition variables to pause unused threads.
   based on the design of thread-pool.scm from sph-lib which has been stress tested in servers and digital signal processing.
   depends on queue.h */
#include <inttypes.h>

#ifndef sph_thread_pool_size_t
#define sph_thread_pool_size_t uint8_t
#endif
#ifndef sph_thread_pool_thread_limit
#define sph_thread_pool_thread_limit 128
#endif
#ifndef sph_thread_pool_task_t_defined
struct sph_thread_pool_task_t;
typedef struct sph_thread_pool_task_t {
  sph_queue_node_t q;
  void (*f)(struct sph_thread_pool_task_t*);
  void* data;
} sph_thread_pool_task_t;
#endif
typedef struct {
  sph_queue_t queue;
  pthread_mutex_t queue_mutex;
  pthread_cond_t queue_not_empty;
  sph_thread_pool_size_t size;
  pthread_t threads[sph_thread_pool_thread_limit];
} sph_thread_pool_t;
typedef void (*sph_thread_pool_task_f_t)(struct sph_thread_pool_task_t*);
void sph_thread_pool_destroy(sph_thread_pool_t* a);
void sph_thread_finish(sph_thread_pool_task_t* task);
void sph_thread_pool_enqueue(sph_thread_pool_t* a, sph_thread_pool_task_t* task);
int sph_thread_pool_finish(sph_thread_pool_t* a, uint8_t no_wait, uint8_t discard_queue);
void* sph_thread_pool_worker(sph_thread_pool_t* a);
int sph_thread_pool_resize(sph_thread_pool_t* a, sph_thread_pool_size_t size, uint8_t no_wait, uint8_t discard_queue);
int sph_thread_pool_new(sph_thread_pool_size_t size, sph_thread_pool_t* a);
#endif
