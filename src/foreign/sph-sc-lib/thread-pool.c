void sph_thread_pool_destroy(sph_thread_pool_t* a) {
  pthread_cond_destroy((&(a->queue_not_empty)));
  pthread_mutex_destroy((&(a->queue_mutex)));
}

/** this is a special task that exits the thread it is being executed in */
void sph_thread_finish(sph_thread_pool_task_t* task) {
  free(task);
  pthread_exit(0);
}

/** add a task to be processed by the next free thread.
   mutexes are used so that the queue is only ever accessed by a single thread */
void sph_thread_pool_enqueue(sph_thread_pool_t* a, sph_thread_pool_task_t* task) {
  pthread_mutex_lock((&(a->queue_mutex)));
  sph_queue_enq((&(a->queue)), (&(task->q)));
  pthread_cond_signal((&(a->queue_not_empty)));
  pthread_mutex_unlock((&(a->queue_mutex)));
}

/** let threads complete all currently enqueued tasks, close the threads and free resources unless no_wait is true.
   if no_wait is true then the call is non-blocking and threads might still be running until they finish the queue after this call.
   thread_pool_finish can be called again without no_wait. with only no_wait thread_pool_destroy will not be called
   and it is unclear when it can be used to free some final resources.
   if discard_queue is true then the current queue is emptied, but note
   that if enqueued tasks free their task object these tasks wont get called anymore */
int sph_thread_pool_finish(sph_thread_pool_t* a, uint8_t no_wait, uint8_t discard_queue) { return ((sph_thread_pool_resize(a, 0, no_wait, discard_queue))); }

/** internal worker routine */
void* sph_thread_pool_worker(sph_thread_pool_t* a) {
  sph_thread_pool_task_t* task;
get_task:
  pthread_mutex_lock((&(a->queue_mutex)));
wait:
  /* considers so-called spurious wakeups */
  if (a->queue.size) {
    task = sph_queue_get((sph_queue_deq((&(a->queue)))), sph_thread_pool_task_t, q);
  } else {
    pthread_cond_wait((&(a->queue_not_empty)), (&(a->queue_mutex)));
    goto wait;
  };
  pthread_mutex_unlock((&(a->queue_mutex)));
  (task->f)(task);
  goto get_task;
}
int sph_thread_pool_resize(sph_thread_pool_t* a, sph_thread_pool_size_t size, uint8_t no_wait, uint8_t discard_queue) {
  pthread_attr_t attr;
  int error;
  void* exit_value;
  sph_thread_pool_size_t i;
  sph_thread_pool_task_t* task;
  if (size > a->size) {
    if (size > sph_thread_pool_thread_limit) {
      return (-1);
    };
    pthread_attr_init((&attr));
    pthread_attr_setdetachstate((&attr), PTHREAD_CREATE_JOINABLE);
    for (i = a->size; (i < size); i += 1) {
      error = pthread_create((i + a->threads), (&attr), ((void* (*)(void*))(sph_thread_pool_worker)), ((void*)(a)));
      if (error) {
        size = i;
        break;
      };
    };
    pthread_attr_destroy((&attr));
    a->size = size;
    return (error);
  } else {
    if (discard_queue) {
      pthread_mutex_lock((&(a->queue_mutex)));
      sph_queue_init((&(a->queue)));
      pthread_mutex_unlock((&(a->queue_mutex)));
    };
    for (i = size; (i < a->size); i += 1) {
      task = malloc((sizeof(sph_thread_pool_task_t)));
      if (!task) {
        return (1);
      };
      task->f = sph_thread_finish;
      sph_thread_pool_enqueue(a, task);
    };
    if (!no_wait) {
      for (i = size; (i < a->size); i += 1) {
        if (!pthread_join(((a->threads)[i]), (&exit_value))) {
          if (i == (a->size - 1)) {
            sph_thread_pool_destroy(a);
          };
        };
      };
    };
    a->size = size;
    return (0);
  };
}

/** returns zero when successful and a non-zero pthread error code otherwise */
int sph_thread_pool_new(sph_thread_pool_size_t size, sph_thread_pool_t* a) {
  sph_thread_pool_size_t i;
  pthread_attr_t attr;
  int error;
  error = 0;
  sph_queue_init((&(a->queue)));
  pthread_mutex_init((&(a->queue_mutex)), 0);
  pthread_cond_init((&(a->queue_not_empty)), 0);
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
