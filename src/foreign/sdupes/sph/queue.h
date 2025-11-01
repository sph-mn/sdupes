
#ifndef sph_queue_h
#define sph_queue_h

/* a fifo queue with the operations enqueue and dequeue that can enqueue custom struct types and a mix of types.
   # example usage
   typedef struct {
     // custom field definitions ...
     sph_queue-_node_t queue_node;
   } element_t;
   element_t e;
   sph_queue_t q;
   sph_queue_init(&q);
   sph_queue_enq(&q, &e.queue_node);
   sph_queue_get(queue_deq(&q), element_t, queue_node); */
#include <stdlib.h>
#include <inttypes.h>
#include <stddef.h>

#define sph_queue_size_t uint32_t

/** returns a pointer to the enqueued struct based on the offset of the sph_queue_node_t field in the struct.
     because of this, queue nodes do not have to be allocated separate from user data.
     downside is that the same user data object can not be contained multiple times.
     must only be called with a non-null pointer that points to the type */
#define sph_queue_get(node, type, field) ((type*)((((char*)(node)) - offsetof(type, field))))
struct sph_queue_node_t;
typedef struct sph_queue_node_t {
  struct sph_queue_node_t* next;
} sph_queue_node_t;
typedef struct {
  sph_queue_size_t size;
  sph_queue_node_t* first;
  sph_queue_node_t* last;
} sph_queue_t;
/** initialize a queue */
void sph_queue_init(sph_queue_t* a) {
  a->first = 0;
  a->last = 0;
  a->size = 0;
}

/** enqueue a node. the node must not already be in the queue. the node must not be null */
void sph_queue_enq(sph_queue_t* a, sph_queue_node_t* node) {
  if (a->first) {
    a->last->next = node;
  } else {
    a->first = node;
  };
  a->last = node;
  node->next = 0;
  a->size = (1 + a->size);
}

/** must only be called when the queue is not empty. a.size can be used to check if the queue is empty */
sph_queue_node_t* sph_queue_deq(sph_queue_t* a) {
  sph_queue_node_t* n;
  n = a->first;
  if (!n->next) {
    a->last = 0;
  };
  a->first = n->next;
  a->size = (a->size - 1);
  return (n);
}
#endif
