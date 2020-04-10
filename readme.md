# sdupes - fast file duplicate detection
at this point in time, many existing file duplication detectors are slow or overengineered. sdupes implements a kind of minimal duplicate file finder that does just one thing.

`sdupes --help`
~~~
usage: sdupes
description
  read file paths from standard input and display paths of excess duplicate files, each set sorted by creation time ascending.
  considers only regular files. files are duplicate if they have identical size, center portion and murmur3 hash
options
  --help, -h  display this help text
  --cluster, -c  display all duplicate paths, two newlines between each set
  --null, -n  for results: use the null byte as path delimiter, two null bytes between each set
~~~

note that given the requirements size/center-portion/hash there is no absolute certainty that files are duplicate, only a high probability that is suitable for many practical use cases. only a costly byte-by-byte comparison can give maximum confidence

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
if nothing is found, nothing is displayed.

it can be used with xargs to remove the files
~~~
find | sdupes | xargs -n 1 -d \\n rm
~~~

# technical details
* all input path names are loaded into memory. in my test, memory usage stayed under 1GB for a 31200 files music library
* original tiny hashtable and array implementations from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) are used
* currently written in c via sc, which just maps scheme-like expressions to c

# possible enhancements
* optionally compare the md5 sum for extra confidence. a nice md5 implementation can be found [here](https://www.nayuki.io/page/fast-md5-hash-implementation-in-x86-assembly)
* optional byte-by-byte comparison. that would be resource intensive because large files cannot be kept in memory. for each comparison, each two relevant files have to be fully read again and again

# license
[gpl3+](https://www.gnu.org/licenses/gpl-3.0.txt)