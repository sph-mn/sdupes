(sc-comment
  "\"array4\" - struct {.current, .data, .size, .used} that combines pointer, length, used length and iteration index in one object.
   includes a current offset, which can be used as an index variable for iteration. particularly useful in macros that should not declare variables.
   this type can be used similar to linked lists, as a drop-in replacement possibly.
   most bindings are generic macros that will work on any array4 type. array4-add and array4-forward go from left to right.
   examples:
     array4_declare_type(my_type, int);
     my_type_t a;
     if(my_type_new(4, &a)) {
       // memory allocation error
     }
     array4_add(a, 1);
     array4_add(a, 2);
     while(array4_in_range(a)) {
       array4_get(a);
       array4_forward(a);
     }
     array4_free(a);")

(pre-include "stdlib.h")

(pre-define
  (array4-declare-type name element-type)
  (array4-declare-type-custom name element-type malloc realloc)
  (array4-declare-type-custom name element-type malloc realloc)
  (begin
    (declare (pre-concat name _t)
      (type (struct (data element-type*) (size size-t) (used size-t) (current size-t))))
    (define ((pre-concat name _new) size a) (uint8-t size-t (pre-concat name _t*))
      "return 0 on success, 1 for memory allocation error"
      (declare data element-type*)
      (set data (malloc (* size (sizeof element-type))))
      (if (not data) (return 1))
      (set a:data data a:size size a:used 0 a:current 0)
      (return 0))
    (define ((pre-concat name _resize) a new-size) (uint8-t (pre-concat name _t*) size-t)
      "return 0 on success, 1 for realloc error"
      (define data element-type* (realloc a:data (* new-size (sizeof element-type))))
      (if (not data) (return 1))
      (set
        a:data data
        a:size new-size
        a:used (if* (< new-size a:used) new-size a:used)
        a:current (if* (< new-size a:current) new-size a:current))
      (return 0)))
  (array4-declare a type) (define a type (struct-literal 0))
  (array4-add a value) (begin (set (array-get a.data a.used) value) (set+ a.used 1))
  (array4-set-null a) (set a.used 0 a.size 0 a.data 0 a.current 0)
  (array4-get-at a index) (array-get a.data index)
  (array4-clear a) (set a.used 0)
  (array4-remove a) (set- a.used 1)
  (array4-size a) a.used
  (array4-max-size a) a.size
  (array4-unused-size a) (- a.size a.used)
  (array4-right-size a) (- a.used a.current)
  (array4-free a) (free a.data)
  (array4-full a) (= a.used a.size)
  (array4-not-full a) (< a.used a.size)
  (array4-take a _data _size _used) (struct-set a data _data size _size used _used)
  (array4-in-range a) (< a.current a.used)
  (array4-get a) (array-get a.data a.current)
  (array4-get-address a) (+ a.data a.current)
  (array4-forward a) (set+ a.current 1)
  (array4-rewind a) (set a.current 0)
  (array4-get-last a) (array-get a.data (- a.used 1)))