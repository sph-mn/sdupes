(sc-comment
  "error handling: message lines on standard error, ignore if possible, exit on memory error.
  ids are indexes in the paths array")

(pre-define _POSIX_C_SOURCE 201000)

(pre-include "inttypes.h" "stdio.h"
  "string.h" "errno.h" "sys/stat.h"
  "sys/mman.h" "fcntl.h" "unistd.h"
  "./foreign/murmur3.c" "./foreign/sph/status.c" "./foreign/sph/hashtable.c"
  "./foreign/sph/i-array.c" "./foreign/sph/helper.c" "./foreign/sph/quicksort.c")

(pre-define
  input-path-count-min 1024
  input-path-count-max 0
  part-checksum-page-count 1
  flag-display-clusters 1
  flag-null-delimiter 2
  (error format ...)
  (fprintf stderr (pre-string-concat "error: %s:%d " format "\n") __func__ __LINE__ __VA-ARGS__)
  memory-error (begin (error "%s" "memory allocation failed") (exit 1)))

(declare
  checksum-t (type (struct (a uint64-t) (b uint64-t)))
  id-t (type uint64-t)
  id-mtime-t (type (struct (id id-t) (mtime uint64-t))))

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

(define (get-sizes paths) (hashtable-64-ids-t paths-t)
  "the result will only contain ids of regular files (no directories, symlinks or similar)"
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

(define (id-mtime-less? a b c) (uint8-t void* ssize-t ssize-t)
  stat-info
  (struct stat)
  (return
    (< (struct-get (array-get (convert-type a id-mtime-t*) b) mtime)
      (struct-get (array-get (convert-type a id-mtime-t*) c) mtime))))

(define (id-mtime-swapper a b c) (void void* ssize-t ssize-t)
  (declare d uint32-t)
  (set
    d (array-get (convert-type a id-mtime-t*) b)
    (array-get (convert-type a id-mtime-t*) b) (array-get (convert-type a id-mtime-t*) c)
    (array-get (convert-type a id-mtime-t*) c) d))

(define (sort-ids-by-mtime ids) (void ids-t)
  (declare stat-info (struct stat))
  (set id-count (i-array-length ids) ids-mtime (malloc (* id-count (sizeof id-t))))
  (if (not ids-mtime) memory-error)
  (sc-comment "get mtime for all ids and sort pairs of id and mtime")
  (for ((set i 0) (<i id-count) (set+ i 1))
    (set file (open path O-RDONLY))
    (if (< file 0) (begin (error "couldnt open %s %s" (strerror errno) path) (return 1)))
    (if (< (fstat file &stat-info) 0)
      (begin (error "couldnt stat %s %s" (strerror errno) path) (close file) (return 1)))
    (close file)
    (struct-set (array-get ids-mtime i) id (ids-get-at ids i) mtime stat-info.st-mtime))
  (quicksort id-mtime-less? id-mtime-swapper ids.start 0 (- ids.unused 1))
  (for ((set i 0) (<i id-count) (set+ i 1))
    (set (array-get ids.start i) (struct-get (array-get ids-mtime i) id)))
  (free ids-mtime))

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
  (if (< (fstat file &stat-info) 0)
    (begin (error "couldnt stat %s %s" (strerror errno) path) (goto error)))
  (if stat-info.st-size
    (if center-page-count
      (if (> stat-info.st-size (* 2 page-size part-checksum-page-count))
        (begin
          (set
            part-start (/ stat-info.st-size page-size)
            part-start (if* (> 3 part-start) page-size (* page-size (/ part-start 2)))
            part-length (* page-size part-checksum-page-count))
          (if (> part-length stat-info.st-size) (set part-length (- stat-info.st-size part-start))))
        (begin (set result:a 0 result:b 0) (goto exit)))
      (set part-start 0 part-length stat-info.st-size))
    (begin (set result:a 0 result:b 0) (goto exit)))
  (set file-buffer (mmap 0 part-length PROT-READ MAP-SHARED file part-start))
  (if (> 0 file-buffer) (begin (error "%s %s\n" (strerror errno) path) (goto error)))
  (MurmurHash3_x64_128 file-buffer part-length 0 temp)
  (set result:a (array-get temp 0) result:b (array-get temp 1))
  (munmap file-buffer part-length)
  (label exit (close file) (return 0))
  (label error (close file) (return 1)))

(define (get-checksums paths ids center-page-count page-size)
  (hashtable-checksum-ids-t paths-t ids-t size-t size-t)
  "assumes that all ids are for regular files"
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
    (if
      (get-checksum (i-array-get-at paths (i-array-get ids)) center-page-count page-size &checksum)
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
  (hashtable-checksum-id-destroy first-ht)
  (return second-ht))

(define (display-result paths ht cluster null) (void paths-t hashtable-checksum-ids-t)
  "also frees hashtable values"
  (declare i size-t ids ids-t)
  (for ((set i 0) (< i ht.size) (set+ i 1))
    (if (not (array-get ht.flags i)) continue)
    (set ids (array-get ht.values i))
    (if cluster (printf "\n") (i-array-forward ids))
    (while (i-array-in-range ids)
      (printf "%s\n" (i-array-get-at paths (i-array-get ids)))
      (i-array-forward ids))
    (i-array-free ids)))

(define (display-help) void
  (printf "usage: sdupes\n")
  (printf "  --help, -h  display this help text\n")
  (printf "  --cluster, -c  display all duplicate paths, two newlines between each set\n")
  (printf "  --null, -c  use the null byte as path delimiter, two null bytes between each set\n"))

(define (cli argc argv) (uint8-t int char**)
  (declare opt int)
  (define options uint8-t 0)
  (define longopts
    (array (struct option) 3
      (struct-literal "help" no-argument 0 #\h) (struct-literal "cluster" no-argument 0 #\c)
      (struct-literal "null" no-argument 0 #\c) (struct-literal 0 0 0 0)))
  (while (not (= -1 (set opt (getopt-long argc argv "chn" longopts 0))))
    (case = opt
      (#\h (display-help) (set options (bit-or flag-exit options)) break)
      (#\c (set options (bit-or flag-display-clusters options)))
      (#\n (set options (bit-or flag-null-delimiter options)))))
  (label exit (return options)))

(define (main argc argv) (int int char**)
  (declare
    sizes-ht hashtable-64-ids-t
    part-checksums-ht hashtable-checksum-ids-t
    i size-t
    j size-t
    options uint8-t
    page-size size-t)
  (i-array-declare paths paths-t)
  (set options (cli argc argv) page-size (sysconf _SC-PAGE-SIZE))
  (if (get-input-paths &paths) memory-error)
  (set sizes-ht (get-sizes paths))
  (for ((set i 0) (< i sizes-ht.size) (set+ i 1))
    (if (not (array-get sizes-ht.flags i)) continue)
    (set part-checksums-ht
      (get-checksums paths (array-get sizes-ht.values i) part-checksum-page-count page-size))
    (i-array-free (array-get sizes-ht.values i))
    (for ((set j 0) (< j part-checksums-ht.size) (set+ j 1))
      (if (not (array-get part-checksums-ht.flags j)) continue)
      (set checksums-ht (get-checksums paths (array-get part-checksums-ht.values j) 0 0))
      (i-array-free (array-get part-checksums-ht.values j))
      (display-result paths checksums-ht
        (bit-and options flag-display-clusters) (bit-and options flag-null-delimiter))
      (hashtable-checksum-ids-free checksums-ht))
    (hashtable-checksum-ids-free part-checksums-ht))
  (return 0))