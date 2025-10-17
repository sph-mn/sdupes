
#ifndef sph_thread_pool_h
#define sph_thread_pool_h

#include <pthread.h>
#include <inttypes.h>
#include <sph/queue.h>
/* thread-pool that uses pthread condition variables to pause unused threads.
   based on the design of thread-pool.scm from sph-lib which has been stress tested in servers and digital signal processing. */

#ifndef sph_thread_pool_size_t
#define sph_thread_pool_size_t uint8_t
#endif
#ifndef sph_thread_pool_thread_limit
#define sph_thread_pool_thread_limit 128
#endif
/* each task object must remain valid for the entire duration of its function call.
   freeing or reusing the object is permitted only after the function returns.
   task functions must not call pthread_exit directly except through sph_thread_finish */
#ifndef sph_thread_pool_task_t_defined
struct sph_thread_pool_task_t;
typedef struct sph_thread_pool_task_t {
  sph_queue_node_t q;
  void (*f)(struct sph_thread_pool_task_t*);
  void* data;
} sph_thread_pool_task_t;
#endif
/* the pool structure is opaque to the caller.
   fields must not be read or modified without holding the internal mutex */
typedef struct {
  sph_queue_t queue;
  pthread_mutex_t queue_mutex;
  pthread_cond_t queue_not_empty;
  sph_thread_pool_size_t size;
  pthread_t threads[sph_thread_pool_thread_limit];
  uint8_t accepting;
  uint8_t shutdown;
} sph_thread_pool_t;
typedef void (*sph_thread_pool_task_f_t)(struct sph_thread_pool_task_t*);
/* the task function must not enqueue this same task again until it returns.
   any synchronization on the tasks data field is the callers responsibility. */
void* sph_thread_pool_worker(void* a);
/* returns zero when successful and a non-zero pthread error code otherwise.
   on nonzero return the pool may be only partially initialized. The caller must invoke finish with no_wait set to true to release all resources */
int sph_thread_pool_new(sph_thread_pool_size_t size, sph_thread_pool_t* a);
/* this is a special task that exits the thread it is being executed in */
void sph_thread_finish(sph_thread_pool_task_t* task);
/* completes all enqueued tasks, closes worker threads, and frees resources unless no_wait is true.
   if no_wait is true, the call returns immediately and remaining tasks may continue running in the background.
   may be invoked again later with no_wait set to false to wait for all threads to exit cleanly.
   must not be called concurrently with destroy or resize. the caller must not enqueue new tasks once this call begins.
   if discard_queue is true, all pending tasks are dropped without execution, and the caller is responsible for any necessary cleanup */
int sph_thread_pool_finish(sph_thread_pool_t* a, uint8_t no_wait, uint8_t discard_queue);
/* requires a non-null task with a valid function pointer. the pool must still be accepting work.
   each task node may be enqueued only once and not reused until its function returns.
   if EBUSY is returned, the pool is no longer accepting work */
int sph_thread_pool_enqueue(sph_thread_pool_t* a, sph_thread_pool_task_t* task);
/* requires the pool to be fully shut down and contain no active threads.
   caller must not invoke concurrently with resize or other control operations */
int sph_thread_pool_destroy(sph_thread_pool_t* a);
/* must not be called concurrently with destroy or another resize.
   the caller must not enqueue new tasks while resizing to zero.
   when increasing size, existing tasks may complete on either the old or new workers.
   if discard_queue is nonzero, the caller accepts that pending tasks are dropped and must perform any necessary cleanup */
int sph_thread_pool_resize(sph_thread_pool_t* a, sph_thread_pool_size_t size, uint8_t no_wait, uint8_t discard_queue);
#endif
