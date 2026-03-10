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

To run them under valgrind:

```sh
cmake --build build --target valgrind
```

Run it with massif:

```sh
valgrind --tool=massif ./build/zipwrapper_tests
massif-visualizer massif.out.*
```

Check the amount of calls with callgrind:

```sh
valgrind --tool=callgrind --callgrind-out-file=callgrind.out.after ./build/zipwrapper_tests
# Check the top calls in our code
callgrind_annotate --auto=yes --inclusive=no callgrind.out.444281 2>&1 | grep -E "(ZipWrapper|ZipHeader|zipios|FileEntry|FileCollection)" | head -20
```

Some callgrind [numbers](https://mbaldessari.github.io/zipwrapper/dev/bench/) recorded over time
