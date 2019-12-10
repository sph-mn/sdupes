(pre-define _POSIX_C_SOURCE 201000 input-path-count-min 1024 input-path-count-max 0)

(pre-include "inttypes.h" "stdio.h"
  "string.h" "errno.h" "sys/stat.h"
  "./foreign/murmur3.c" "./foreign/sph/hashtable.c" "./foreign/sph/i-array.c"
  "./foreign/sph/status.c" "./foreign/sph/helper.c")

(i-array-declare-type ids uint64-t)
(i-array-declare-type paths uint8-t*)
(hashtable-declare-type hashtable-64-id uint64-t uint64-t)
(hashtable-declare-type hashtable-64-ids uint64-t ids-t)

(define (get-input-paths output) (uint8-t paths-t*)
  (i-array-declare result paths-t)
  (declare line char* line-copy char* line-size size-t char-count ssize-t)
  (set line 0 line-size 0)
  (if (paths-new input-path-count-min &result) (return 1))
  (set char-count (getline &line &line-size stdin))
  (while (not (= -1 char-count))
    (if (not line-size) continue)
    (if
      (and (> (i-array-length result) (i-array-max-length result))
        (paths-resize &result (* 2 (i-array-max-length result))))
      (return 1))
    (sc-comment "getline returns the delimiter if present")
    (if (and char-count (= #\newline (array-get line (- char-count 1))))
      (set (array-get line (- char-count 1)) 0 line-size (- line-size 1)))
    (set line-copy (malloc line-size))
    (if (not line-copy) (return 1))
    (memcpy line-copy line line-size)
    (i-array-add result line-copy)
    (set char-count (getline &line &line-size stdin)))
  (set *output result)
  (return 0))

(define (get-sizes paths) (hashtable-64-ids-t paths-t)
  (declare
    error int
    existing uint64-t*
    second-existing ids-t*
    stat-info (struct stat)
    first-ht hashtable-64-id-t
    second-ht hashtable-64-ids-t)
  (i-array-declare ids ids-t)
  (hashtable-64-id-new (i-array-length paths) &first-ht)
  (hashtable-64-ids-new (i-array-length paths) &second-ht)
  (while (i-array-in-range paths)
    (set error (stat (i-array-get paths) &stat-info))
    (if error (fprintf stderr "error: get-sizes: %s %s\n" (strerror errno) (i-array-get paths)))
    (if (not (S-ISREG stat-info.st_mode)) (begin (i-array-forward paths) continue))
    (set existing (hashtable-64-id-get first-ht stat-info.st-size))
    (if existing
      (begin
        (set second-existing (hashtable-64-ids-get second-ht stat-info.st-size))
        (if second-existing
          (begin
            (printf "%lu")
            (sc-comment "resize value if necessary")
            (if (= (i-array-length *second-existing) (i-array-max-length *second-existing))
              (if (ids-resize second-existing (* 2 (i-array-max-length *second-existing)))
                (begin (exit 1) (fprintf stderr "error: get-sizes: memory allocation failed\n"))))
            (i-array-add *second-existing (i-array-get-index paths)))
          (begin
            (if (ids-new 4 &ids)
              (begin (exit 1) (fprintf stderr "error: get-sizes: memory allocation failed\n")))
            (i-array-add ids *existing)
            (i-array-add ids (i-array-get-index paths))
            (hashtable-64-ids-set second-ht stat-info.st-size ids))))
      (hashtable-64-id-set first-ht stat-info.st-size (i-array-get-index paths)))
    (i-array-forward paths))
  (declare i size-t)
  (for ((set i 0) (< i second-ht.size) (set+ i 1))
    (if (array-get second-ht.flags i)
      (begin
        (printf "duplicate set\n")
        (while (i-array-in-range (array-get second-ht.values i))
          (printf "  %s\n" (i-array-get-at paths (i-array-get (array-get second-ht.values i))))
          (i-array-forward (array-get second-ht.values i))))))
  (return second-ht))

#;(define (get-checksums paths) (hashtable-64-ids-t paths-t)
  (declare
    error int
    existing uint64-t*
    second-existing ids-t*
    first-ht hashtable-64-id-t
    second-ht hashtable-64-ids-t)
  (i-array-declare ids ids-t)
  (hashtable-64-id-new (i-array-length paths) &first-ht)
  (hashtable-64-ids-new (i-array-length paths) &second-ht)
  (while (i-array-in-range paths)
    (set error (stat (i-array-get paths) &stat-info))
    (if error (fprintf stderr "error: get-sizes: %s %s\n" (strerror errno) (i-array-get paths)))
    (if (not (S-ISREG stat-info.st_mode)) (begin (i-array-forward paths) continue))
    (set existing (hashtable-64-id-get first-ht stat-info.st-size))
    (if existing
      (begin
        (set second-existing (hashtable-64-ids-get second-ht stat-info.st-size))
        (if second-existing
          (begin
            (printf "%lu")
            (sc-comment "resize value if necessary")
            (if (= (i-array-length *second-existing) (i-array-max-length *second-existing))
              (if (ids-resize second-existing (* 2 (i-array-max-length *second-existing)))
                (begin (exit 1) (fprintf stderr "error: get-sizes: memory allocation failed\n"))))
            (i-array-add *second-existing (i-array-get-index paths)))
          (begin
            (if (ids-new 4 &ids)
              (begin (exit 1) (fprintf stderr "error: get-sizes: memory allocation failed\n")))
            (i-array-add ids *existing)
            (i-array-add ids (i-array-get-index paths))
            (hashtable-64-ids-set second-ht stat-info.st-size ids))))
      (hashtable-64-id-set first-ht stat-info.st-size (i-array-get-index paths)))
    (i-array-forward paths)))

(define (main) int
  (i-array-declare paths paths-t)
  (if (not (= (sizeof uint64-t) (sizeof char*)))
    (begin (fprintf stderr "error: program assumes that pointers are 64 bit") (return 1)))
  (if (get-input-paths &paths)
    (begin (fprintf stderr "error: memory allocation error while reading input paths") (return 1)))
  ;(get-sizes paths)
  (declare output (array uint64-t 2))
  (MurmurHash3_x64_128 "test" 4 48 output)
  (printf "%llx %llx\n" (array-get output 0) (array-get output 1))
  (return 0))