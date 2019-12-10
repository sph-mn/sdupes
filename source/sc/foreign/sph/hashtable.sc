(pre-include "stdlib.h" "inttypes.h")

(sc-comment "a macro that defines hash-table data types for arbitrary key/value types and values,"
  "using linear probing for collision resolve,"
  "with hash and equal functions customisable by defining macros and re-including the source.")

(pre-define
  ; example hashing code
  (hashtable-hash-integer key hashtable) (modulo key hashtable.size)
  (hashtable-equal-integer key-a key-b) (= key-a key-b))

(pre-define-if-not-defined
  hashtable-size-factor 2
  hashtable-hash hashtable-hash-integer
  hashtable-equal hashtable-equal-integer)

(declare hashtable-primes
  (array uint32-t ()
    ; from https://planetmath.org/goodhashtableprimes
    #f 53 97
    193 389 769
    1543 3079 6151
    12289 24593 49157
    98317 196613 393241
    786433 1572869 3145739
    6291469 12582917 25165843 50331653 100663319 201326611 402653189 805306457 1610612741))

(define hashtable-primes-end uint32-t* (+ hashtable-primes 26))

(define (hashtable-calculate-size min-size) (size-t size-t)
  (set min-size (* hashtable-size-factor min-size))
  (define primes uint32-t* hashtable-primes)
  (while (< primes hashtable-primes-end)
    (if (<= min-size *primes) (return *primes) (set primes (+ 1 primes))))
  (if (<= min-size *primes) (return *primes))
  ; if no prime has been found, use size-factor times size made odd as a best guess
  (return (bit-or 1 min-size)))

(pre-define (hashtable-declare-type name key-type value-type)
  (begin
    (declare (pre-concat name _t)
      (type (struct (size size-t) (flags uint8-t*) (keys key-type*) (values value-type*))))
    (define ((pre-concat name _new) min-size result) (uint8-t size-t (pre-concat name _t*))
      ; returns 0 on success or 1 if the memory allocation failed
      (declare flags uint8-t* keys key-type* values value-type*)
      (set min-size (hashtable-calculate-size min-size))
      (set flags (calloc min-size 1))
      (if (not flags) (return 1))
      (set keys (calloc min-size (sizeof key-type)))
      (if (not keys) (begin (free flags) (return 1)))
      (set values (malloc (* min-size (sizeof value-type))))
      (if (not values) (begin (free keys) (free flags) (return 1)))
      (struct-set *result flags flags keys keys values values size min-size)
      (return 0))
    (define ((pre-concat name _destroy) a) (void (pre-concat name _t))
      (begin (free a.values) (free a.keys) (free a.flags)))
    (define ((pre-concat name _get) a key) (value-type* (pre-concat name _t) key-type)
      "returns the address of the value in the hash table, 0 if it was not found"
      (declare i size-t hash-i size-t)
      (set hash-i (hashtable-hash key a) i hash-i)
      (while (< i a.size)
        (if (array-get a.flags i)
          (if (hashtable-equal key (array-get a.keys i)) (return (+ i a.values)))
          (return 0))
        (set+ i 1))
      (sc-comment "wraps over")
      (set i 0)
      (while (< i hash-i)
        (if (array-get a.flags i)
          (if (hashtable-equal key (array-get a.keys i)) (return (+ i a.values)))
          (return 0))
        (set+ i 1))
      (return 0))
    (define ((pre-concat name _set) a key value)
      (value-type* (pre-concat name _t) key-type value-type)
      "returns the address of the added or already included value, 0 if there is no space left in the hash table"
      (declare i size-t hash-i size-t)
      (set hash-i (hashtable-hash key a) i hash-i)
      (while (< i a.size)
        (if (array-get a.flags i)
          (if (hashtable-equal key (array-get a.keys i)) (return (+ i a.values)) (set+ i 1))
          (begin
            (set (array-get a.flags i) #t (array-get a.keys i) key (array-get a.values i) value)
            (return (+ i a.values)))))
      (set i 0)
      (while (< i hash-i)
        (if (array-get a.flags i)
          (if (hashtable-equal key (array-get a.keys i)) (return (+ i a.values)) (set+ i 1))
          (begin
            (set (array-get a.flags i) #t (array-get a.keys i) key (array-get a.values i) value)
            (return (+ i a.values)))))
      (return 0))
    (define ((pre-concat name _remove) a key) (uint8-t (pre-concat name _t) key-type)
      "returns 0 if the element was removed, 1 if it was not found.
       only needs to set flag to zero"
      (define value value-type* ((pre-concat name _get) a key))
      (if value (begin (set (array-get a.flags (- value a.values)) 0) (return 0)) (return 1)))))