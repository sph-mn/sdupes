
#include <errno.h>
#include <stdlib.h>
int sph_thread_pool_destroy(sph_thread_pool_t* a) {
  int result = 0;
  pthread_mutex_lock((&(a->queue_mutex)));
  if (a->size || !a->shutdown) {
    pthread_mutex_unlock((&(a->queue_mutex)));
    return (EBUSY);
  };
  pthread_mutex_unlock((&(a->queue_mutex)));
  pthread_cond_destroy((&(a->queue_not_empty)));
  pthread_mutex_destroy((&(a->queue_mutex)));
  return (result);
}
int sph_thread_pool_enqueue(sph_thread_pool_t* a, sph_thread_pool_task_t* task) {
  int result = 0;
  if (!task) {
    return (EINVAL);
  };
  if (!task->f) {
    return (EINVAL);
  };
  pthread_mutex_lock((&(a->queue_mutex)));
  if (!a->accepting) {
    pthread_mutex_unlock((&(a->queue_mutex)));
    return (EBUSY);
  };
  sph_queue_enq((&(a->queue)), (&(task->q)));
  pthread_mutex_unlock((&(a->queue_mutex)));
  pthread_cond_signal((&(a->queue_not_empty)));
  return (result);
}
void* sph_thread_pool_worker(void* b) {
  sph_thread_pool_t* a = b;
  sph_thread_pool_task_t* task = 0;
  while (1) {
    pthread_mutex_lock((&(a->queue_mutex)));
    while (!(a->queue.size || a->shutdown)) {
      pthread_cond_wait((&(a->queue_not_empty)), (&(a->queue_mutex)));
    };
    if (!a->queue.size && a->shutdown) {
      pthread_mutex_unlock((&(a->queue_mutex)));
      return (0);
    };
    task = sph_queue_get((sph_queue_deq((&(a->queue)))), sph_thread_pool_task_t, q);
    pthread_mutex_unlock((&(a->queue_mutex)));
    (task->f)(task);
  };
}

/** must be called only by serialized control logic.
   sets accepting to false, marks the pool as shutting down, and wakes all waiting threads. */
static void sph_thread_pool_request_shutdown(sph_thread_pool_t* a) {
  pthread_mutex_lock((&(a->queue_mutex)));
  a->accepting = 0;
  a->shutdown = 1;
  pthread_mutex_unlock((&(a->queue_mutex)));
  pthread_cond_broadcast((&(a->queue_not_empty)));
}
void sph_thread_finish(sph_thread_pool_task_t* task) {
  free(task);
  pthread_exit(0);
}
int sph_thread_pool_finish(sph_thread_pool_t* a, uint8_t no_wait, uint8_t discard_queue) { return ((sph_thread_pool_resize(a, 0, no_wait, discard_queue))); }
int sph_thread_pool_new(sph_thread_pool_size_t size, sph_thread_pool_t* a) {
  sph_thread_pool_size_t i;
  pthread_attr_t attr;
  int error;
  error = 0;
  sph_queue_init((&(a->queue)));
  pthread_mutex_init((&(a->queue_mutex)), 0);
  pthread_cond_init((&(a->queue_not_empty)), 0);
  a->accepting = 1;
  a->shutdown = 0;
  pthread_attr_init((&attr));
  pthread_attr_setdetachstate((&attr), PTHREAD_CREATE_JOINABLE);
  for (i = 0; (i < size); i += 1) {
    error = pthread_create((i + a->threads), (&attr), ((void* (*)(void*))(sph_thread_pool_worker)), ((void*)(a)));
    if (error) {
      if (0 < i) {
        /* try to finish previously created threads */
        a->size = i;
        sph_thread_pool_finish(a, 1, 0);
      };
      goto exit;
    };
  };
  a->size = size;
exit:
  pthread_attr_destroy((&attr));
  return (error);
}
int sph_thread_pool_resize(sph_thread_pool_t* a, sph_thread_pool_size_t size, uint8_t no_wait, uint8_t discard_queue) {
  pthread_attr_t attr;
  void* join_value;
  sph_thread_pool_size_t i;
  sph_thread_pool_task_t* task;
  int error_code = 0;
  if (size > sph_thread_pool_thread_limit) {
    return (EINVAL);
  };
  if (size > a->size) {
    pthread_attr_init((&attr));
    pthread_attr_setdetachstate((&attr), PTHREAD_CREATE_JOINABLE);
    i = a->size;
    while ((i < size)) {
      if (pthread_create((a->threads + i), (&attr), sph_thread_pool_worker, a)) {
        pthread_mutex_lock((&(a->queue_mutex)));
        a->size = i;
        pthread_mutex_unlock((&(a->queue_mutex)));
        error_code = EAGAIN;
        i = size;
      } else {
        i += 1;
      };
    };
    pthread_attr_destroy((&attr));
    pthread_mutex_lock((&(a->queue_mutex)));
    a->accepting = 1;
    a->shutdown = 0;
    a->size = size;
    pthread_mutex_unlock((&(a->queue_mutex)));
    return (error_code);
  };
  if (!size) {
    sph_thread_pool_request_shutdown(a);
  };
  if (discard_queue) {
    pthread_mutex_lock((&(a->queue_mutex)));
    sph_queue_init((&(a->queue)));
    pthread_mutex_unlock((&(a->queue_mutex)));
    pthread_cond_broadcast((&(a->queue_not_empty)));
  };
  if (size) {
    i = size;
    while ((i < a->size)) {
      task = malloc((sizeof(sph_thread_pool_task_t)));
      if (!task) {
        return (ENOMEM);
      };
      task->f = sph_thread_finish;
      if (sph_thread_pool_enqueue(a, task)) {
        free(task);
        return (EBUSY);
      };
      i += 1;
    };
  };
  if (!no_wait) {
    i = size;
    while ((i < a->size)) {
      pthread_join(((a->threads)[i]), (&join_value));
      i += 1;
    };
    pthread_mutex_lock((&(a->queue_mutex)));
    a->size = size;
    pthread_mutex_unlock((&(a->queue_mutex)));
    if (!size) {
      sph_thread_pool_destroy(a);
    };
  } else {
    pthread_mutex_lock((&(a->queue_mutex)));
    a->size = size;
    pthread_mutex_unlock((&(a->queue_mutex)));
  };
  return (0);
}
