# sdupes - fast file duplicate detection
at this point in time, many existing file duplication detectors seemed slow or overengineered. sdupes implements a kind of minimal duplicate file finder that does just one thing.

`sdupes --help`
~~~
usage: sdupes
description
  read file paths from standard input and display paths of excess duplicate files sorted by modification time ascending.
  considers only regular files with differing device id and inode. files are duplicate if all of the following properties match:
  * size
  * murmur3 hash of a center portion
  * name or content
options
  --help, -h  display this help text
  --cluster, -c  display all duplicate paths. two newlines between sets
  --ignore-filenames, -b  always do a full byte-by-byte comparison, even if size, hash, and name are equal
  --null, -0  use a null byte to delimit paths. two null bytes between sets
  --sort-reverse, -s  sort clusters by modification time descending
~~~

# dependencies
* c 2011 standard library (for example glibc)
* posix 2008 features (linux, freebsd)
* for the provided compile script: shell, gcc
* optional: [sph-sc](https://github.com/sph-mn/sph-sc) and clang-format to edit the code in sc

# installation
~~~
sh ./exe/compile-c
~~~
this should build the file `temp/sdupes`, which is the final executable and can be taken and copied anywhere, for example into `/usr/bin` (as root) after which sdupes should be available as a command on the command-line (if the file has the execute bit set).

# usage
~~~
find /home/myuser/directory-with-10000s-of-large-files | sdupes
~~~

lists excess duplicate files. the first file is always left out of each set except when --cluster is used.
if no duplicates are found, nothing is displayed.

it can be used with xargs to remove duplicate files:
~~~
find | sdupes | xargs -n 1 -d \\n rm
~~~

# technical details
* all input path names are loaded into memory. in my test, memory usage stayed under 1GB for a 31200 files music library
* tiny hashtable and array implementations from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) are used
* currently written in c via sc, which maps scheme-like syntax to readable c. the prepared c code is available under source/c-precompiled

# license
[gpl3+](https://www.gnu.org/licenses/gpl-3.0.txt)
