# sdupes - fast duplicate file detection

at this point in time, many duplicate file finders are slow or overengineered. sdupes reads paths from standard input and writes paths of duplicate files to standard output.

# usage
## list duplicates
~~~
find /home/myuser/directory-with-10000s-of-large-files | sdupes
~~~

this lists excess duplicate files. the oldest file of each set is always left out except when --cluster is used.
if no duplicates are found, nothing is displayed.

## remove duplicates
sdupes can be used with xargs to remove duplicate files:
~~~
find | sdupes | xargs -n 1 -d \\n rm
~~~

## sdupes --help
~~~
usage: sdupes
description
  read file paths from standard input and display paths of excess duplicate files sorted by creation time ascending.
  considers only regular files with differing device and inode. files are duplicate if all of the following properties match:
  * size
  * murmur3 hashes of start, middle, and end portions
  * name or content
options
  --help, -h  display this help text
  --cluster, -c  display all paths with duplicates. two newlines between sets
  --ignore-name, -n  do not consider file names
  --ignore-content, -d  do not consider the full file content
  --null, -0  use a null byte to delimit paths. two null bytes between sets
  --reverse, -r  sort clusters by creation time descending
  --version, -v  show the running program version number
~~~

# dependencies
* c 2011 standard library (for example musl-libc or glibc)
* posix 2008 features (for example linux or freebsd)
* for the provided compile script: shell, gcc

# installation
~~~
sh ./exe/compile
~~~
this should create `exe/sdupes` which is the statically compiled final executable and can be taken and copied anywhere. for example, it can be copied or symlinked into `/usr/bin` (as root) after which sdupes should be available as a command on the command-line (if the file has the execute bit set).

# technical details
* all input path names are loaded into memory. in my test, memory usage stayed under 1GB for a 31200 files music library
* tiny hashtable and set implementations from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) are used

# license
[gpl3+](https://www.gnu.org/licenses/gpl-3.0.txt)

# performance comparison
quick tests using bash "time" comparing sdupes to other popular duplicate finders.

714G in 27697 files with 800M in 11 duplicates:
* sdupes: 0m0.119s
* app1: 0m2.024s
* app2: 0m3.913s
* app3: 0m10.755s

634G in 538 files with 21G in 67 duplicates:
* sdupes: 0m0.005s (0m2.380s with --ignore-filenames)
* app1: 1m11.140s
* app2: 0m57.232s
* app3: 2m41.197s

# tests
* exe/valgrind-run checks for memory leaks
* exe/md5comparison-run compares the md5 sums of duplicates

# possible enhancements
* allow usage as a library by not necessarily exiting the process
* parallelization. this can increase throughput on ssds and distributed storage
  * the file size clustering can be split into batches. the per-cluster checksum and byte comparisons are independent
  * [futures.h](https://github.com/sph-mn/sph-sc-lib/blob/master/source/c-precompiled/sph/futures.h)

# similar projects
* [rmlint](https://github.com/sahib/rmlint)
* [fdupes](https://github.com/adrianlopezroche/fdupes)
* [duff](https://github.com/jcburley/duff)
