# MiniRedis

An in-memory key-value server in C++17, inspired by Redis.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
