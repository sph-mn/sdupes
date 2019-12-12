(define (quicksort less? swap array left right)
  (void (function-pointer uint8-t void* ssize-t ssize-t)
    (function-pointer void void* ssize-t ssize-t) void* ssize-t ssize-t)
  "a generic quicksort implementation that works with any array type.
   less should return true if the first argument is < than the second.
   swap should exchange the values of the two arguments it receives.
   quicksort(less, swap, array, 0, array-size - 1)"
  (if (<= right left) return)
  (define pivot ssize-t (+ left (/ (- right left) 2)))
  (define l ssize-t left)
  (define r ssize-t right)
  (while #t
    (while (less? array l pivot) (set l (+ 1 l)))
    (while (less? array pivot r) (set r (- r 1)))
    (if (> l r) break)
    (cond ((= pivot l) (set pivot r)) ((= pivot r) (set pivot l)))
    (swap array l r)
    (set l (+ 1 l) r (- r 1)))
  (quicksort less? swap array left r)
  (quicksort less? swap array l right))