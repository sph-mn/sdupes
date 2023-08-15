(sc-comment
  "error handling: message lines on standard error. ignore files and continue if possible but exit on memory errors.
   ids are indices of the paths array")

(pre-define _POSIX_C_SOURCE 201000)

(pre-include "inttypes.h" "stdio.h"
  "string.h" "errno.h" "sys/stat.h"
  "sys/mman.h" "fcntl.h" "unistd.h"
  "getopt.h" "foreign/murmur3.c" "foreign/sph-sc-lib/hashtable.h"
  "foreign/sph-sc-lib/set.h" "foreign/sph-sc-lib/array4.h" "foreign/sph-sc-lib/quicksort.h")

(pre-define
  input-path-allocate-min 1024
  checksum-portion-size 16384
  flag-display-clusters 1
  flag-null-delimiter 2
  flag-exit 4
  flag-reverse 8
  flag-ignore-filenames 16
  (error format ...)
  (fprintf stderr (pre-string-concat "error: %s:%d " format "\n") __func__ __LINE__ __VA-ARGS__)
  memory-error (begin (error "%s" "memory allocation failed") (exit 1))
  (checksum-hash key size) (modulo (array-get key.data 1) size)
  (checksum-equal key-a key-b) (not (memcmp key-a.data key-b.data 48))
  (device-and-inode-hash key size) (modulo key.inode size)
  (device-and-inode-equal key-a key-b)
  (and (= key-a.inode key-b.inode) (= key-a.device key-b.device))
  (ids-add-with-resize a id)
  (if (and (= (array4-size a) (array4-max-size a)) (ids-resize &a (* 2 (array4-max-size a))))
    memory-error
    (array4-add a id))
  (array4-free-elements a free)
  (begin (array4-rewind a) (while (array4-in-range a) (free (array4-get a)) (array4-forward a))))

(declare
  checksum-t (type (struct (data (array uint64-t 6))))
  id-t (type size-t)
  id-time-t (type (struct (id id-t) (time time-t)))
  device-and-inode-t (type (struct (device dev-t) (inode ino-t))))

(array4-declare-type ids id-t)
(array4-declare-type paths uint8-t*)

(sph-hashtable-declare-type id-by-size off-t
  id-t sph-hashtable-hash-integer sph-hashtable-equal-integer 2)

(sph-hashtable-declare-type ids-by-size off-t
  ids-t sph-hashtable-hash-integer sph-hashtable-equal-integer 2)

(sph-hashtable-declare-type id-by-checksum checksum-t id-t checksum-hash checksum-equal 2)
(sph-hashtable-declare-type ids-by-checksum checksum-t ids-t checksum-hash checksum-equal 2)
(define device-and-inode-null device-and-inode-t (struct-literal 0))

(sph-set-declare-type-nonull device-and-inode-set device-and-inode-t
  device-and-inode-hash device-and-inode-equal device-and-inode-null 2)

(define (simple-basename path) (uint8-t* uint8-t*)
  (define slash-pointer uint8-t* (strrchr path #\/))
  (return (if* slash-pointer (+ 1 slash-pointer) path)))

(define (file-open path out) (uint8-t uint8-t* int*)
  (set *out (open path O-RDONLY))
  (if (< *out 0) (begin (error "could not open %s %s" (strerror errno) path) (return 1)))
  (return 0))

(define (file-stat file path out) (uint8-t int uint8-t* (struct stat*))
  (if (fstat file out) (begin (error "could not stat %s %s" (strerror errno) path) (return 1)))
  (return 0))

(define (file-size file path out) (uint8-t int uint8-t* off-t*)
  (declare stat-info (struct stat))
  (if (file-stat file path &stat-info) (return 1))
  (set *out stat-info.st-size)
  (return 0))

(define (file-mmap file size out) (uint8-t int off-t uint8-t**)
  (set *out (mmap 0 size PROT-READ MAP-SHARED file 0))
  (if (= MAP-FAILED *out) (begin (error "%s" (strerror errno)) (return 1)))
  (return 0))

(define (id-time-less? a b c) (uint8-t void* ssize-t ssize-t)
  (return
    (< (struct-get (array-get (convert-type a id-time-t*) b) time)
      (struct-get (array-get (convert-type a id-time-t*) c) time))))

(define (id-time-greater? a b c) (uint8-t void* ssize-t ssize-t)
  (return
    (> (struct-get (array-get (convert-type a id-time-t*) b) time)
      (struct-get (array-get (convert-type a id-time-t*) c) time))))

(define (id-time-swapper a b c) (void void* ssize-t ssize-t)
  (declare d id-time-t)
  (set
    d (array-get (convert-type a id-time-t*) b)
    (array-get (convert-type a id-time-t*) b) (array-get (convert-type a id-time-t*) c)
    (array-get (convert-type a id-time-t*) c) d))

(define (sort-ids-by-ctime ids paths sort-descending) (uint8-t ids-t paths-t uint8-t)
  "sort ids in-place via temporary array of pairs of id and ctime"
  (declare
    id id-t
    id-count size-t
    ids-time id-time-t*
    file int
    path uint8-t*
    stat-info (struct stat))
  (set id-count (array4-size ids) ids-time (malloc (* id-count (sizeof id-time-t))))
  (if (not ids-time) memory-error)
  (for ((define i size-t 0) (< i id-count) (set+ i 1))
    (set id (array4-get-at ids i) path (array4-get-at paths id))
    (if (file-open path &file) continue)
    (if (not (file-stat file path &stat-info))
      (struct-set (array-get ids-time i) id id time stat-info.st-ctime))
    (close file))
  (quicksort (if* sort-descending id-time-greater? id-time-less?) id-time-swapper
    ids-time 0 (- id-count 1))
  (for ((define i size-t 0) (< i id-count) (set+ i 1))
    (set (array4-get-at ids i) (struct-get (array-get ids-time i) id)))
  (free ids-time)
  (return 0))

(define (display-help) void
  (printf "usage: sdupes\n")
  (printf "description\n")
  (printf
    "  read file paths from standard input and display paths of excess duplicate files sorted by creation time ascending.\n")
  (printf
    "  considers only regular files with differing device and inode. files are duplicate if all of the following properties match:\n")
  (printf "  * size\n")
  (printf "  * murmur3 hashes of start, middle, and end portions\n")
  (printf "  * name or content\n")
  (printf "options\n")
  (printf "  --help, -h  display this help text\n")
  (printf "  --cluster, -c  display all duplicate paths. two newlines between sets\n")
  (printf
    "  --ignore-filenames, -b  always do a full byte-by-byte comparison, even if size, hashes, and name are equal\n")
  (printf "  --null, -0  use a null byte to delimit paths. two null bytes between sets\n")
  (printf "  --reverse, -r  sort clusters by creation time descending\n"))

(define (cli argc argv) (uint8-t int char**)
  (declare
    opt int
    options uint8-t
    longopts
    (array (struct option) 6
      (struct-literal "help" no-argument 0 #\h) (struct-literal "cluster" no-argument 0 #\c)
      (struct-literal "null" no-argument 0 #\0) (struct-literal "reverse" no-argument 0 #\s)
      (struct-literal "ignore-filenames" no-argument 0 #\b) (struct-literal 0)))
  (set options 0)
  (while (not (= -1 (set opt (getopt-long argc argv "ch0rb" longopts 0))))
    (case = opt
      (#\h (display-help) (set options (bit-or flag-exit options)) break)
      (#\c (set options (bit-or flag-display-clusters options)))
      (#\0 (set options (bit-or flag-null-delimiter options)))
      (#\r (set options (bit-or flag-reverse options)))
      (#\b (set options (bit-or flag-ignore-filenames options)))))
  (return options))

(define (get-input-paths) paths-t
  "read newline separated paths from standard input and return in paths_t array"
  (declare paths paths-t line char* line-copy char* line-size size-t char-count ssize-t)
  (set line 0 line-size 0)
  (if (paths-new input-path-allocate-min &paths) memory-error)
  (set char-count (getline &line &line-size stdin))
  (while (not (= -1 char-count))
    (if (not line-size) continue)
    (if
      (and (> (array4-size paths) (array4-max-size paths))
        (paths-resize &paths (* 2 (array4-max-size paths))))
      memory-error)
    (sc-comment "getline always returns the delimiter if not end of input")
    (if (and char-count (= #\newline (array-get line (- char-count 1))))
      (set (array-get line (- char-count 1)) 0 line-size (- line-size 1)))
    (set line-copy (malloc line-size))
    (if (not line-copy) memory-error)
    (memcpy line-copy line line-size)
    (array4-add paths line-copy)
    (set char-count (getline &line &line-size stdin)))
  (if (= ENOMEM errno) memory-error)
  (free line)
  (return paths))

(define (get-duplicated-ids-by-size paths) (ids-by-size-t paths-t)
  "the result will only contain ids of regular files (no directories, symlinks, etc)"
  (declare
    id-by-size id-by-size-t
    id id-t*
    ids-by-size ids-by-size-t
    ids ids-t*
    new-ids ids-t
    device-and-inode device-and-inode-t
    device-and-inode-set device-and-inode-set-t
    stat-info (struct stat))
  (if
    (or (id-by-size-new (array4-size paths) &id-by-size)
      (ids-by-size-new (array4-size paths) &ids-by-size)
      (device-and-inode-set-new (array4-size paths) &device-and-inode-set))
    memory-error)
  (for ((define i id-t 0) (< i (array4-size paths)) (set+ i 1))
    (if (lstat (array4-get-at paths i) &stat-info)
      (begin (error "could not lstat %s %s" (strerror errno) (array4-get-at paths i)) continue))
    (if (not (S-ISREG stat-info.st_mode)) continue)
    (struct-set device-and-inode device stat-info.st-dev inode stat-info.st-ino)
    (if (device-and-inode-set-get device-and-inode-set device-and-inode) continue
      (device-and-inode-set-add device-and-inode-set device-and-inode))
    (set id (id-by-size-get id-by-size stat-info.st-size))
    (if (not id) (begin (id-by-size-set id-by-size stat-info.st-size i) continue))
    (set ids (ids-by-size-get ids-by-size stat-info.st-size))
    (if ids (begin (ids-add-with-resize *ids i) continue))
    (if (ids-new 2 &new-ids) memory-error)
    (array4-add new-ids *id)
    (array4-add new-ids i)
    (ids-by-size-set ids-by-size stat-info.st-size new-ids))
  (id-by-size-free id-by-size)
  (device-and-inode-set-free device-and-inode-set)
  (return ids-by-size))

(define (get-checksum path out) (uint8-t uint8-t* checksum-t*)
  (declare buffer uint8-t* file int stat-info (struct stat))
  (define null checksum-t (struct-literal 0))
  (set *out null)
  (if (file-open path &file) (return 1))
  (if (file-stat file path &stat-info) (begin (close file) (return 1)))
  (if (not stat-info.st-size) (begin (close file) (return 0)))
  (set buffer (mmap 0 stat-info.st-size PROT-READ MAP-SHARED file 0))
  (close file)
  (if (= MAP-FAILED buffer) (begin (error "%s" (strerror errno)) (return 1)))
  (if (< stat-info.st-size (* 2 3 checksum-portion-size))
    (MurmurHash3_x64_128 buffer stat-info.st-size 0 out:data)
    (begin
      (MurmurHash3_x64_128 buffer checksum-portion-size 0 out:data)
      (MurmurHash3_x64_128 (+ (- (/ stat-info.st-size 2) (/ checksum-portion-size 2)) buffer)
        checksum-portion-size 0 (+ 2 out:data))
      (MurmurHash3_x64_128 (+ (- stat-info.st-size checksum-portion-size) buffer)
        checksum-portion-size 0 (+ 4 out:data))))
  (munmap buffer stat-info.st-size)
  (return 0))

(define (get-ids-by-checksum paths ids) (ids-by-checksum-t paths-t ids-t)
  "assumes that all ids are of regular files"
  (declare
    checksum checksum-t
    checksum-id id-t*
    checksum-ids ids-t*
    id id-t
    id-by-checksum id-by-checksum-t
    ids-by-checksum ids-by-checksum-t
    new-checksum-ids ids-t)
  (if
    (or (id-by-checksum-new (array4-size ids) &id-by-checksum)
      (ids-by-checksum-new (array4-size ids) &ids-by-checksum))
    memory-error)
  (while (array4-in-range ids)
    (set id (array4-get ids))
    (if (get-checksum (array4-get-at paths id) &checksum)
      (error "could not calculate checksum for %s" (array4-get-at paths id)))
    (set checksum-id (id-by-checksum-get id-by-checksum checksum))
    (if checksum-id
      (begin
        (set checksum-ids (ids-by-checksum-get ids-by-checksum checksum))
        (if checksum-ids (ids-add-with-resize *checksum-ids id)
          (begin
            (if (ids-new 2 &new-checksum-ids) memory-error)
            (array4-add new-checksum-ids *checksum-id)
            (array4-add new-checksum-ids id)
            (ids-by-checksum-set ids-by-checksum checksum new-checksum-ids))))
      (id-by-checksum-set id-by-checksum checksum id))
    (array4-forward ids))
  (id-by-checksum-free id-by-checksum)
  (return ids-by-checksum))

(define (get-duplicates paths ids ignore-filenames) (ids-t paths-t ids-t uint8-t)
  "return ids whose file name or file content is equal"
  (declare
    content uint8-t*
    file int
    first-content uint8-t*
    first-name uint8-t*
    id id-t
    name uint8-t*
    path uint8-t*
    size off-t)
  (array4-declare duplicates ids-t)
  (if (not (array4-in-range ids)) (return duplicates))
  (set id (array4-get ids) path (array4-get-at paths id) first-name (simple-basename path))
  (if (file-open path &file) (return duplicates))
  (if (file-size file path &size) (begin (close file) (return duplicates)))
  (if (not size) (begin (close file) (return ids)))
  (if (file-mmap file size &first-content) (begin (close file) (return duplicates)))
  (close file)
  (if (ids-new (array4-size ids) &duplicates) memory-error)
  (array4-add duplicates id)
  (array4-forward ids)
  (while (array4-in-range ids)
    (set id (array4-get ids) path (array4-get-at paths id) name (simple-basename path))
    (if (or ignore-filenames (strcmp first-name name))
      (begin
        (if (file-open path &file) continue)
        (if (file-mmap file size &content) (begin (close file) continue))
        (close file)
        (if (not (memcmp first-content content size)) (array4-add duplicates id))
        (munmap content size))
      (array4-add duplicates id))
    (array4-forward ids))
  (munmap first-content size)
  (if (= 1 (array4-size duplicates)) (array4-remove duplicates))
  (return duplicates))

(define (display-duplicates paths ids delimiter cluster-count display-cluster reverse)
  (void paths-t ids-t uint8-t id-t uint8-t uint8-t)
  "assumes that ids contains at least two entries"
  (if (sort-ids-by-ctime ids paths reverse) return)
  (if display-cluster (if cluster-count (putchar delimiter)) (array4-forward ids))
  (do-while (array4-in-range ids)
    (printf "%s%c" (array4-get-at paths (array4-get ids)) delimiter)
    (array4-forward ids)))

(define (main argc argv) (int int char**)
  (declare
    delimiter uint8-t
    duplicates ids-t
    ids-by-checksum ids-by-checksum-t
    ids-by-size ids-by-size-t
    ids ids-t
    options uint8-t
    cluster-count id-t
    paths paths-t)
  (set options (cli argc argv))
  (if (bit-and flag-exit options) (exit 0))
  (set
    delimiter (if* (bit-and options flag-null-delimiter) 0 #\newline)
    paths (get-input-paths)
    ids-by-size (get-duplicated-ids-by-size paths)
    cluster-count 0)
  (for ((define i size-t 0) (< i ids-by-size.size) (set+ i 1))
    (if (not (array-get ids-by-size.flags i)) continue)
    (set ids (array-get ids-by-size.values i) ids-by-checksum (get-ids-by-checksum paths ids))
    (array4-free ids)
    (for ((define j size-t 0) (< j ids-by-checksum.size) (set+ j 1))
      (if (not (array-get ids-by-checksum.flags j)) continue)
      (set
        ids (array-get ids-by-checksum.values j)
        duplicates (get-duplicates paths ids (bit-and options flag-ignore-filenames)))
      (if (not (= duplicates.data ids.data)) (array4-free ids))
      (if (< 1 (array4-size duplicates))
        (begin
          (display-duplicates paths duplicates
            delimiter cluster-count (bit-and options flag-display-clusters)
            (bit-and options flag-reverse))
          (set+ cluster-count 1)))
      (array4-free duplicates))
    (ids-by-checksum-free ids-by-checksum))
  (ids-by-size-free ids-by-size)
  (array4-free-elements paths free)
  (array4-free paths)
  (return 0))