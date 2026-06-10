# simple-cloud-storage

Minimal C++ experiments for file transfer.

## Local file transfer without the server

Build:

```sh
cmake -S . -B build
cmake --build build
```

If CMake is not installed, the current single-binary version can also be built directly:

```sh
mkdir -p build
clang++ -std=c++17 -Iclient/include -Icommon client/src/client.cpp -o build/local_transfer
```

Start a receiver in one terminal:

```sh
./build/local_transfer recv 9090 ./received
```

Send a file from another terminal:

```sh
./build/local_transfer send 127.0.0.1 9090 ./path/to/file.txt
```

The receiver accepts one connection, writes the file into the output directory,
then exits.
