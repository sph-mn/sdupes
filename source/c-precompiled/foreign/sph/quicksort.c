
/* depends on sys/types.h for ssize_t */

/** a generic quicksort implementation that works with any array type.
   less should return true if the first argument is < than the second.
   swap should exchange the values of the two arguments it receives.
   quicksort(less, swap, array, 0, array-size - 1) */
void quicksort(uint8_t (*less_p)(void*, ssize_t, ssize_t), void (*swap)(void*, ssize_t, ssize_t), void* array, ssize_t left, ssize_t right) {
  if (right <= left) {
    return;
  };
  ssize_t pivot = (left + ((right - left) / 2));
  ssize_t l = left;
  ssize_t r = right;
  while (1) {
    while (less_p(array, l, pivot)) {
      l = (1 + l);
    };
    while (less_p(array, pivot, r)) {
      r = (r - 1);
    };
    if (l > r) {
      break;
    };
    if (pivot == l) {
      pivot = r;
    } else if (pivot == r) {
      pivot = l;
    };
    swap(array, l, r);
    l = (1 + l);
    r = (r - 1);
  };
  quicksort(less_p, swap, array, left, r);
  quicksort(less_p, swap, array, l, right);
}
