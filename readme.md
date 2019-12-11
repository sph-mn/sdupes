# sdupes - fast file duplicate detection
at this point in time, many existing file duplication detectors are slow or overengineered. sdupes implements a kind of minimal duplicate file finder that does just one thing.

* input: list of newline separated file paths to standard input. this way paths can even be pre-filtered with "find", for example
* files considered duplicate if having: identical size, identical center portion and full file murmur3 checksum
* output: double-newline separated newline separated lists of duplicates

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
this should build the file `temp/sdupes`, which is the final executable and can be taken and copied anywhere, for example into /usr/bin (as root), then you have sdupes as a command on the command-line.


# usage
~~~
find /home/myuser/directory-with-1000s-of-huge-files | sdupes
~~~

in case duplicates are found, the result looks similar to this
~~~
/home/nonroot/temp/picture/duplicates/15418__340.png
/home/nonroot/temp/picture/15418__340.png

/home/nonroot/temp/picture/duplicates/1477673299231.png
/home/nonroot/temp/picture/1477673299231.png

/home/nonroot/temp/picture/duplicates/wz79o7vixhy11.jpg
/home/nonroot/temp/picture/wz79o7vixhy11.jpg
~~~

if nothing is found, nothing is displayed.

sdupes has no cli help or options.

## how to work with the result
there is no prepared solution yet for further using the results.
something like this can work and excludes the first found duplicate, showing only excess duplicate files:
~~~
find path | temp/sdupes | ruby -e '$stdin.read.split("\n\n").map{|a| a = a.split("\n"); a.shift(); puts a.join("\n")}'
~~~

# technical details
* all input paths are loaded into memory
* an original, tiny hashtable and array implementation from [sph-sc-lib](https://github.com/sph-mn/sph-sc-lib) is used
* currently written in c via sc, which maps scheme-like expressions to c

# possible enhancements
* byte-by-byte comparison, but that is relatively resource intensive
* cli --help option
* output format that lists only excess duplicates and can be passed to xargs

# license
gpl3+