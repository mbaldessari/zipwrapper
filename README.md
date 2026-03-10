# zipwrapper++

A thin C++17 wrapper around [libzip](https://libzip.org/) that provides
stream-based zip I/O classes (read, write, sequential iteration, gzip
compression).

Just a temporary project to play with this potential replacement for freecad

## Dependencies

- libzip
- zlib
- GTest (for tests)

## Build

```sh
cmake -B build
cmake --build build
```

## Run tests

```sh
./build/zipwrapper_tests
```
