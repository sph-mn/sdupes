# sdupes - fast file duplicate detection
at this point in time, many existing file duplication detectors are slow or overengineered. sdupes implements a kind of minimal duplicate file finder that does just one thing.

* input: list of newline separated file paths to standard input. this way paths can even be pre-filtered with "find", for example
* files considered duplicate if having: identical size, identical center portion and full file murmur3 checksum
* output: double-newline separated newline separated lists of paths

# dependencies
* c 2011 standard library (for example glibc)
* posix 2008 features (linux, freebsd)
* 64 bit processor
* for the provided compile script: shell, gcc
* optional: sph-sc and clang-format to edit the sc code

# installation
~~~
sh ./exe/compile-c
~~~
this should build the file `temp/sdupes`, which is the final executable and can be taken and copied anywhere, for example into `/usr/bin` (as root), then you have sdupes as a command on the command-line.

# usage
~~~
find /home/myuser/directory-with-1000s-of-huge-files | sdupes
~~~

lists excess duplicate files. one file is always left out of each set except when --cluster is used.

`sdupes --help`
~~~
usage: sdupes
description
  read file paths from standard input and display excess duplicate files, each set sorted by creation time ascending.
  considers only regular files. files are considered duplicate if they have the same size and murmur3 hash
options
  --help, -h  display this help text
  --cluster, -c  display all duplicate paths, two newlines between each set
  --null, -n  for results: use the null byte as path delimiter, two null bytes between each set
~~~

if nothing is found, nothing is displayed.

# technical details
* all input path names are loaded into memory
* an original, tiny hashtable and array implementation from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) is used
* currently written in c via sc, which just maps scheme-like expressions to c

# possible enhancements
* byte-by-byte comparison - if needed. but it is resource intensive

# license
gpl3+