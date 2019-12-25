(sc-comment
  "error handling: message lines on standard error, ignore if possible, exit on memory error.
  ids are indexes in the paths array")

(pre-define _POSIX_C_SOURCE 201000)

(pre-include "inttypes.h" "stdio.h"
  "string.h" "errno.h" "sys/stat.h"
  "sys/mman.h" "fcntl.h" "unistd.h"
  "getopt.h" "./foreign/murmur3.c" "./foreign/sph/status.c"
  "./foreign/sph/hashtable.c" "./foreign/sph/i-array.c" "./foreign/sph/helper.c"
  "./foreign/sph/quicksort.c")

(pre-define
  input-path-count-min 1024
  input-path-count-max 0
  part-checksum-page-count 1
  flag-display-clusters 1
  flag-null-delimiter 2
  flag-exit 4
  flag-sort-reverse 8
  (error format ...)
  (fprintf stderr (pre-string-concat "error: %s:%d " format "\n") __func__ __LINE__ __VA-ARGS__)
  memory-error (begin (error "%s" "memory allocation failed") (exit 1)))

(declare
  checksum-t (type (struct (a uint64-t) (b uint64-t)))
  id-t (type uint64-t)
  id-ctime-t (type (struct (id id-t) (ctime uint64-t))))

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

(define (id-ctime-less? a b c) (uint8-t void* ssize-t ssize-t)
  (return
    (< (struct-get (array-get (convert-type a id-ctime-t*) b) ctime)
      (struct-get (array-get (convert-type a id-ctime-t*) c) ctime))))

(define (id-ctime-greater? a b c) (uint8-t void* ssize-t ssize-t)
  (return
    (> (struct-get (array-get (convert-type a id-ctime-t*) b) ctime)
      (struct-get (array-get (convert-type a id-ctime-t*) c) ctime))))

(define (id-ctime-swapper a b c) (void void* ssize-t ssize-t)
  (declare d id-ctime-t)
  (set
    d (array-get (convert-type a id-ctime-t*) b)
    (array-get (convert-type a id-ctime-t*) b) (array-get (convert-type a id-ctime-t*) c)
    (array-get (convert-type a id-ctime-t*) c) d))

(define (sort-ids-by-ctime ids paths sort-descending) (uint8-t ids-t paths-t uint8-t)
  "sort ids in-place via temporary array of pairs of id and ctime"
  (declare
    file int
    i size-t
    id-count size-t
    stat-info (struct stat)
    path uint8-t*
    ids-ctime id-ctime-t*)
  (set id-count (i-array-length ids) ids-ctime (malloc (* id-count (sizeof id-ctime-t))))
  (if (not ids-ctime) memory-error)
  (for ((set i 0) (< i id-count) (set+ i 1))
    (set path (i-array-get-at paths (i-array-get-at ids i)) file (open path O-RDONLY))
    (if (< file 0)
      (begin (error "couldnt open %s %s" (strerror errno) path) (free ids-ctime) (return 1)))
    (if (fstat file &stat-info)
      (begin
        (error "couldnt stat %s %s" (strerror errno) path)
        (close file)
        (free ids-ctime)
        (return 1)))
    (close file)
    (struct-set (array-get ids-ctime i) id (i-array-get-at ids i) ctime stat-info.st-ctime))
  (quicksort (if* sort-descending id-ctime-greater? id-ctime-less?) id-ctime-swapper
    ids-ctime 0 (- id-count 1))
  (for ((set i 0) (< i id-count) (set+ i 1))
    (set (array-get ids.start i) (struct-get (array-get ids-ctime i) id)))
  (free ids-ctime)
  (return 0))

(define (display-help) void
  (printf "usage: sdupes\n")
  (printf "description\n")
  (printf
    "  read file paths from standard input and display excess duplicate files, each set sorted by creation time ascending.\n")
  (printf
    "  considers only regular files. files are duplicate if they have the same size, center portion and murmur3 hash\n")
  (printf "options\n")
  (printf "  --help, -h  display this help text\n")
  (printf "  --cluster, -c  display all duplicate paths, two newlines between each set\n")
  (printf
    "  --null, -n for results: use the null byte as path delimiter, two null bytes between each set\n")
  (printf "  --sort-reverse, -s  sort clusters by creation time descending\n"))

(define (cli argc argv) (uint8-t int char**)
  (declare
    opt int
    longopts
    (array (struct option) 5
      (struct-literal "help" no-argument 0 #\h) (struct-literal "cluster" no-argument 0 #\c)
      (struct-literal "null" no-argument 0 #\n) (struct-literal "sort-reverse" no-argument 0 #\s)
      (struct-literal 0 0 0 0)))
  (define options uint8-t 0)
  (while (not (= -1 (set opt (getopt-long argc argv "chns" longopts 0))))
    (case = opt
      (#\h (display-help) (set options (bit-or flag-exit options)) break)
      (#\c (set options (bit-or flag-display-clusters options)))
      (#\n (set options (bit-or flag-null-delimiter options)))
      (#\s (set options (bit-or flag-sort-reverse options)))))
  (return options))

(define (get-checksum path center-page-count page-size result)
  (uint8-t uint8-t* size-t size-t checksum-t*)
  "center-page-count and page-size are optional and can be zero.
   if center-page-count is not zero, only a centered part of the file is checksummed.
   in this case page-size must also be non-zero"
  (declare
    file int
    file-buffer uint8-t*
    stat-info (struct stat)
    temp (array uint64-t 2)
    part-start size-t
    part-length size-t)
  (set file (open path O-RDONLY))
  (if (< file 0) (begin (error "couldnt open %s %s" (strerror errno) path) (return 1)))
  (if (fstat file &stat-info)
    (begin (error "couldnt stat %s %s" (strerror errno) path) (close file) (return 1)))
  (if stat-info.st-size
    (if center-page-count
      (if (> stat-info.st-size (* 2 page-size part-checksum-page-count))
        (begin
          (set
            part-start (/ stat-info.st-size page-size)
            part-start (if* (> 3 part-start) page-size (* page-size (/ part-start 2)))
            part-length (* page-size part-checksum-page-count))
          (if (> part-length stat-info.st-size) (set part-length (- stat-info.st-size part-start))))
        (begin (set result:a 0 result:b 0) (close file) (return 0)))
      (set part-start 0 part-length stat-info.st-size))
    (begin (set result:a 0 result:b 0) (close file) (return 0)))
  (if (not page-size) (set part-length stat-info.st-size))
  ;(printf "start: %lu, end: %lu, size: %lu, path: %s\n" part-start
  ;  (+ part-start part-length) stat-info.st-size path)
  (set file-buffer (mmap 0 part-length PROT-READ MAP-SHARED file part-start))
  (close file)
  (if (= MAP-FAILED file-buffer) (begin (error "%s" (strerror errno)) (return 1)))
  (MurmurHash3_x64_128 file-buffer part-length 0 temp)
  (munmap file-buffer part-length)
  (set result:a (array-get temp 0) result:b (array-get temp 1))
  (return 0))

(define (get-input-paths) paths-t
  (i-array-declare result paths-t)
  (declare line char* line-copy char* line-size size-t char-count ssize-t)
  (set line 0 line-size 0)
  (if (paths-new input-path-count-min &result) memory-error)
  (set char-count (getline &line &line-size stdin))
  (while (not (= -1 char-count))
    (if (not line-size) continue)
    (if
      (and (> (i-array-length result) (i-array-max-length result))
        (paths-resize &result (* 2 (i-array-max-length result))))
      memory-error)
    (sc-comment "getline always returns the delimiter if not end of input")
    (if (and char-count (= #\newline (array-get line (- char-count 1))))
      (set (array-get line (- char-count 1)) 0 line-size (- line-size 1)))
    (set line-copy (malloc line-size))
    (if (not line-copy) memory-error)
    (memcpy line-copy line line-size)
    (i-array-add result line-copy)
    (set char-count (getline &line &line-size stdin)))
  (if (= ENOMEM errno) memory-error)
  (free line)
  (return result))

(define (get-sizes paths) (hashtable-64-ids-t paths-t)
  "the result will only contain ids of regular files (no directories, symlinks or similar)"
  (declare
    existing1 id-t*
    existing2 ids-t*
    ht1 hashtable-64-id-t
    ht2 hashtable-64-ids-t
    stat-info (struct stat))
  (i-array-declare ids ids-t)
  (if
    (or (hashtable-64-id-new (i-array-length paths) &ht1)
      (hashtable-64-ids-new (i-array-length paths) &ht2))
    memory-error)
  (while (i-array-in-range paths)
    (if (stat (i-array-get paths) &stat-info)
      (error "%s %s\n" (strerror errno) (i-array-get paths)))
    (if (not (S-ISREG stat-info.st_mode)) (begin (i-array-forward paths) continue))
    (set existing1 (hashtable-64-id-get ht1 stat-info.st-size))
    (if existing1
      (begin
        (set existing2 (hashtable-64-ids-get ht2 stat-info.st-size))
        (if existing2
          (begin
            (if (= (i-array-length *existing2) (i-array-max-length *existing2))
              (if (ids-resize existing2 (* 2 (i-array-max-length *existing2))) memory-error))
            (i-array-add *existing2 (i-array-get-index paths)))
          (begin
            (if (ids-new 2 &ids) memory-error)
            (i-array-add ids *existing1)
            (i-array-add ids (i-array-get-index paths))
            (hashtable-64-ids-set ht2 stat-info.st-size ids))))
      (hashtable-64-id-set ht1 stat-info.st-size (i-array-get-index paths)))
    (i-array-forward paths))
  (hashtable-64-id-free ht1)
  (return ht2))

(define (get-checksums paths ids center-page-count page-size)
  (hashtable-checksum-ids-t paths-t ids-t size-t size-t)
  "assumes that all ids are for regular files"
  (declare
    checksum checksum-t
    existing1 id-t*
    existing2 ids-t*
    ht1 hashtable-checksum-id-t
    ht2 hashtable-checksum-ids-t)
  (i-array-declare value-ids ids-t)
  (if
    (or (hashtable-checksum-id-new (i-array-length ids) &ht1)
      (hashtable-checksum-ids-new (i-array-length ids) &ht2))
    memory-error)
  (while (i-array-in-range ids)
    (if
      (get-checksum (i-array-get-at paths (i-array-get ids)) center-page-count page-size &checksum)
      (error "couldnt calculate checksum for %s" (i-array-get-at paths (i-array-get ids))))
    (set existing1 (hashtable-checksum-id-get ht1 checksum))
    (if existing1
      (begin
        (set existing2 (hashtable-checksum-ids-get ht2 checksum))
        (if existing2
          (begin
            (if (= (i-array-length *existing2) (i-array-max-length *existing2))
              (if (ids-resize existing2 (* 2 (i-array-max-length *existing2))) memory-error))
            (i-array-add *existing2 (i-array-get ids)))
          (begin
            (if (ids-new 2 &value-ids) memory-error)
            (i-array-add value-ids *existing1)
            (i-array-add value-ids (i-array-get ids))
            (hashtable-checksum-ids-set ht2 checksum value-ids))))
      (hashtable-checksum-id-set ht1 checksum (i-array-get ids)))
    (i-array-forward ids))
  (hashtable-checksum-id-free ht1)
  (return ht2))

(define (display-result paths ht cluster null sort-reverse)
  (void paths-t hashtable-checksum-ids-t uint8-t uint8-t uint8-t)
  "also frees hashtable and its values"
  (declare i size-t ids ids-t delimiter uint8-t)
  (set delimiter (if* null #\0 #\newline))
  (for ((set i 0) (< i ht.size) (set+ i 1))
    (if (not (array-get ht.flags i)) continue)
    (set ids (array-get ht.values i))
    (if (sort-ids-by-ctime ids paths sort-reverse) continue)
    (if cluster (printf "%c" delimiter) (i-array-forward ids))
    (while (i-array-in-range ids)
      (printf "%s%c" (i-array-get-at paths (i-array-get ids)) delimiter)
      (i-array-forward ids))
    (i-array-free ids))
  (hashtable-checksum-ids-free ht))

(define (main argc argv) (int int char**)
  (declare
    sizes-ht hashtable-64-ids-t
    part-checksums-ht hashtable-checksum-ids-t
    i size-t
    j size-t
    options uint8-t
    page-size size-t)
  (i-array-declare paths paths-t)
  (set options (cli argc argv))
  (if (bit-and flag-exit options) (exit 0))
  (set page-size (sysconf _SC-PAGE-SIZE) paths (get-input-paths) sizes-ht (get-sizes paths))
  (for ((set i 0) (< i sizes-ht.size) (set+ i 1))
    (if (not (array-get sizes-ht.flags i)) continue)
    (set part-checksums-ht
      (get-checksums paths (array-get sizes-ht.values i) part-checksum-page-count page-size))
    (i-array-free (array-get sizes-ht.values i))
    (for ((set j 0) (< j part-checksums-ht.size) (set+ j 1))
      (if (not (array-get part-checksums-ht.flags j)) continue)
      (display-result paths (get-checksums paths (array-get part-checksums-ht.values j) 0 0)
        (bit-and options flag-display-clusters) (bit-and options flag-null-delimiter)
        (bit-and options flag-sort-reverse))
      (i-array-free (array-get part-checksums-ht.values j)))
    (hashtable-checksum-ids-free part-checksums-ht))
  (return 0))