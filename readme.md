# sdupes - fast file duplicate detection
at this point in time, many existing file duplication detectors are slow or overengineered. sdupes implements a kind of minimal duplicate file finder that does just one thing.

`sdupes --help`
~~~
usage: sdupes
description
  read file paths from standard input and display paths of excess duplicate files, each set sorted by creation time ascending.
  considers only regular files. files are duplicate if all of the following properties are identical:
  * file size
  * murmur3 hash of a center portion of size page size
  * murmur3 hash of the whole file
options
  --help, -h  display this help text
  --cluster, -c  display all duplicate paths, two newlines between each set
  --null, -n  for results: use the null byte as path delimiter, two null bytes between each set
~~~

note that given the requirements of identical size/center-portion-hash/hash, there is no absolute certainty that files are duplicate, only a probability high enough for many practical use cases. only a costly byte-by-byte comparison can give absolute confidence. if unsure, run with `--cluster` and check the results

# dependencies
* c 2011 standard library (for example glibc)
* posix 2008 features (linux, freebsd)
* 64 bit processor
* for the provided compile script: shell, gcc
* optional: [sph-sc](https://github.com/sph-mn/sph-sc) and clang-format to edit the sc code

# installation
~~~
sh ./exe/compile-c
~~~
this should build the file `temp/sdupes`, which is the final executable and can be taken and copied anywhere, for example into `/usr/bin` (as root), then you have sdupes as a command on the command-line.

# usage
~~~
find /home/myuser/directory-with-1000s-of-large-files | sdupes
~~~

lists excess duplicate files. the first file is always left out of each set except when --cluster is used.
if no duplicates are found, nothing is displayed.

it can be used with xargs to remove the files
~~~
find | sdupes | xargs -n 1 -d \\n rm
~~~

# technical details
* all input path names are loaded into memory. in my test, memory usage stayed under 1GB for a 31200 files music library
* original tiny hashtable and array implementations from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) are used
* currently written in c via sc, which maps scheme-like syntax to c

# possible enhancements
* optional md5sum comparison, for extra confidence. a nice md5 implementation can be found [here](https://www.nayuki.io/page/fast-md5-hash-implementation-in-x86-assembly)
* optional byte-by-byte comparison. this can be resource intensive because large files cannot be kept in memory. each two relevant files have to be fully read again and again for comparison

# license
[gpl3+](https://www.gnu.org/licenses/gpl-3.0.txt)