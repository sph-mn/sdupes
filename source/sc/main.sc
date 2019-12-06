(pre-define _POSIX_C_SOURCE 201000 input-path-count-min 1024 input-path-count-max 0)

(pre-include "inttypes.h" "stdio.h"
  "string.h" "./foreign/sph/imht-set.c" "./foreign/sph/i-array.c"
  "./foreign/sph/status.c" "./foreign/sph/helper.c")

(i-array-declare-type i-array-b64 uint64-t)

(define (get-input-paths output) (uint8-t i-array-b64*)
  (i-array-declare result i-array-b64)
  (declare line char* line-copy char* line-size size-t)
  (set line 0 line-size 0)
  (if (i-array-allocate-i-array-b64 input-path-count-min &result) (return 1))
  (while (not (= -1 (getline &line &line-size stdin)))
    (if (not line-size) continue)
    (if
      (and (> (i-array-length result) (i-array-max-length result))
        (i-array-resize-i-array-b64 &result (* 2 (i-array-max-length result))))
      (return 1))
    (set line-copy (malloc line-size))
    (if (not line-copy) (return 1))
    (memcpy line-copy line line-size)
    (i-array-add result (convert-type line-copy uint64-t)))
  (set *output result)
  (return 0))

(define (main) int
  (i-array-declare paths i-array-b64)
  (if (not (= (sizeof uint64-t) (sizeof char*)))
    (begin (fprintf stderr "error: program assumes that pointers are 64 bit") (return 1)))
  (if (get-input-paths &paths)
    (begin (fprintf stderr "error: memory allocation error while reading input paths") (return 1)))
  (while (i-array-in-range paths) (printf "%s" (i-array-get paths)) (i-array-forward paths))
  (return 0))