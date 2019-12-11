(pre-define _POSIX_C_SOURCE 201000 input-path-count-min 1024 input-path-count-max 0)

(pre-include "inttypes.h" "stdio.h"
  "string.h" "errno.h" "sys/stat.h"
  "sys/mman.h" "fcntl.h" "unistd.h"
  "./foreign/murmur3.c" "./foreign/sph/status.c" "./foreign/sph/hashtable.c"
  "./foreign/sph/i-array.c" "./foreign/sph/helper.c")

(declare checksum-t (type (struct (a uint64-t) (b uint64-t))) id-t (type uint64-t))
(i-array-declare-type ids id-t)
(i-array-declare-type paths uint8-t*)
(hashtable-declare-type hashtable-64-id uint64-t id-t)
(hashtable-declare-type hashtable-64-ids uint64-t ids-t)
(pre-undefine hashtable-hash hashtable-equal)

(pre-define
  (hashtable-hash key hashtable) (modulo key.a hashtable.size)
  (hashtable-equal key-a key-b) (and (= key-a.a key-b.a) (= key-a.b key-b.b)))

(hashtable-declare-type hashtable-checksum-id checksum-t id-t)
(hashtable-declare-type hashtable-checksum-ids checksum-t ids-t)

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
    (sc-comment "getline always returns the delimiter if not end of input")
    (if (and char-count (= #\newline (array-get line (- char-count 1))))
      (set (array-get line (- char-count 1)) 0 line-size (- line-size 1)))
    (set line-copy (malloc line-size))
    (if (not line-copy) (return 1))
    (memcpy line-copy line line-size)
    (i-array-add result line-copy)
    (set char-count (getline &line &line-size stdin)))
  (set *output result)
  (return 0))

(pre-define
  (error format ...)
  (fprintf stderr (pre-string-concat "error: %s:%d " format "\n") __func__ __LINE__ __VA-ARGS__)
  memory-error (begin (error "%s" "memory allocation failed") (exit 1)))

(define (get-sizes paths) (hashtable-64-ids-t paths-t)
  (declare
    status int
    existing id-t*
    second-existing ids-t*
    stat-info (struct stat)
    first-ht hashtable-64-id-t
    second-ht hashtable-64-ids-t)
  (i-array-declare ids ids-t)
  (if
    (or (hashtable-64-id-new (i-array-length paths) &first-ht)
      (hashtable-64-ids-new (i-array-length paths) &second-ht))
    memory-error)
  (while (i-array-in-range paths)
    (set status (stat (i-array-get paths) &stat-info))
    (if status (error "%s %s\n" (strerror errno) (i-array-get paths)))
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
                memory-error))
            (i-array-add *second-existing (i-array-get-index paths)))
          (begin
            (if (ids-new 4 &ids) memory-error)
            (i-array-add ids *existing)
            (i-array-add ids (i-array-get-index paths))
            (hashtable-64-ids-set second-ht stat-info.st-size ids))))
      (hashtable-64-id-set first-ht stat-info.st-size (i-array-get-index paths)))
    (i-array-forward paths))
  (hashtable-64-id-destroy first-ht)
  (return second-ht))

(define (get-checksum path result) (uint8-t uint8-t* checksum-t*)
  (declare file int file-buffer uint8-t* stat-info (struct stat) temp (array uint64-t 2))
  (set file (open path O-RDONLY))
  (if (< file 0) (begin (error "%s %s" "couldnt open file at " path) (return 1)))
  (if (< (fstat file &stat-info) 0) (begin (error "%s %s" "couldnt stat file at " path) (return 1)))
  (set file-buffer (mmap 0 stat-info.st-size PROT-READ MAP-SHARED file 0))
  (MurmurHash3_x64_128 file-buffer stat-info.st-size 0 temp)
  (set result:a (array-get temp 0) result:b (array-get temp 1))
  (munmap file-buffer stat-info.st-size)
  (close file)
  (return 0))

(define (get-checksums paths ids) (hashtable-checksum-ids-t paths-t ids-t)
  "assumes that paths are regular files"
  (declare
    checksum checksum-t
    existing id-t*
    first-ht hashtable-checksum-id-t
    second-existing ids-t*
    second-ht hashtable-checksum-ids-t)
  (i-array-declare value-ids ids-t)
  (if
    (or (hashtable-checksum-id-new (i-array-length ids) &first-ht)
      (hashtable-checksum-ids-new (i-array-length ids) &second-ht))
    memory-error)
  (while (i-array-in-range ids)
    (if (get-checksum (i-array-get-at paths (i-array-get ids)) &checksum)
      (error "%s" "couldnt calculate checksum for " (i-array-get-at paths (i-array-get ids))))
    (set existing (hashtable-checksum-id-get first-ht checksum))
    (if existing
      (begin
        (set second-existing (hashtable-checksum-ids-get second-ht checksum))
        (if second-existing
          (begin
            (sc-comment "resize value if necessary")
            (if (= (i-array-length *second-existing) (i-array-max-length *second-existing))
              (if (ids-resize second-existing (* 2 (i-array-max-length *second-existing)))
                memory-error))
            (i-array-add *second-existing (i-array-get ids)))
          (begin
            (if (ids-new 4 &value-ids) memory-error)
            (i-array-add value-ids *existing)
            (i-array-add value-ids (i-array-get ids))
            (hashtable-checksum-ids-set second-ht checksum value-ids))))
      (hashtable-checksum-id-set first-ht checksum (i-array-get ids)))
    (i-array-forward ids))
  (return second-ht))

(define (main) int
  (declare
    sizes-ht hashtable-64-ids-t
    checksums-ht hashtable-checksum-ids-t
    sizes-i size-t
    checksums-i size-t)
  (i-array-declare paths paths-t)
  (if (get-input-paths &paths) memory-error)
  (set sizes-ht (get-sizes paths))
  (for ((set sizes-i 0) (< sizes-i sizes-ht.size) (set+ sizes-i 1))
    (if (array-get sizes-ht.flags sizes-i)
      (begin
        (set checksums-ht (get-checksums paths (array-get sizes-ht.values sizes-i)))
        (for ((set checksums-i 0) (< checksums-i checksums-ht.size) (set+ checksums-i 1))
          (if (array-get checksums-ht.flags checksums-i)
            (begin
              (printf "\n")
              (while (i-array-in-range (array-get checksums-ht.values checksums-i))
                (printf "%s\n"
                  (i-array-get-at paths (i-array-get (array-get checksums-ht.values checksums-i))))
                (i-array-forward (array-get checksums-ht.values checksums-i))))))
        (hashtable-checksum-ids-destroy checksums-ht))))
  (return 0))