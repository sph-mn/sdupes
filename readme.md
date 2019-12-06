# sdupes - superfast file duplicate detection

at this point, many existing file duplication detectors are slow or overengineered. sdupes implements a kind of minimal duplicate file finder that does just one thing.

* input: a list of newline separated file paths read from standard input. this way the paths can be pre-filtered, for example with "find"
* processing: group paths by identical size, then identical center portion, then identical checksum (to be implemented: possibly murmur3)
* output: double-newline separated newline separated lists of duplicates

# dependencies
* c 2011 standard library (for example glibc)
* a posix 2008 system (linux, freebsd)
* for the provided compile script: shell, gcc
* optional: sph-sc and clang-format to edit the sc code

# technical details
* all input paths are loaded into memory
* an original, tiny hashtable and array implementation is used
* currently written in c via sc, which maps scheme-like expressions to c

# possible enhancements
* byte-by-byte comparison is costly, is it needed?

# license
gpl3+