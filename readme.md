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
  considers only regular files with differing device id and inode. files are duplicate if all of the following properties match:
  * size
  * murmur3 hashes of start, middle, and end portions
  * name or content
options
  --help, -h  display this help text
  --cluster, -c  display all duplicate paths. two newlines between sets
  --ignore-filenames, -b  always do a full byte-by-byte comparison, even if size, hashes, and name are equal
  --null, -0  use a null byte to delimit paths. two null bytes between sets
  --sort-reverse, -s  sort clusters by creation time descending
~~~

# dependencies
* c 2011 standard library (for example glibc)
* posix 2008 features (linux, freebsd)
* for the provided compile script: shell, gcc
* optional: [sph-sc](https://github.com/sph-mn/sph-sc) and clang-format to edit the code in sc

# license
[gpl3+](https://www.gnu.org/licenses/gpl-3.0.txt)

# installation
~~~
sh ./exe/compile-c
~~~
this should create `temp/sdupes` which is the final executable and can be taken and copied anywhere. for example, it can be copied or symlinked into `/usr/bin` (as root) after which sdupes should be available as a command on the command-line (if the file has the execute bit set).

# technical details
* all input path names are loaded into memory. in my test, memory usage stayed under 1GB for a 31200 files music library
* tiny hashtable and array implementations from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) are used
* written in c, currently via sc which maps scheme-like syntax to readable c. the c code is available under source/c-precompiled

# performance comparison
quick tests using bash "time".

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

# similar projects
* [rmlint](https://github.com/sahib/rmlint)
* [fdupes](https://github.com/adrianlopezroche/fdupes)
* [duff](https://github.com/jcburley/duff)