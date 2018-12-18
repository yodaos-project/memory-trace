# memory-trace
take C/C++ heap snapshot, currently supported the following features:
- file and line number
- backtrace call stack, up to 10 stacktraces

## usage
1. add `./include/memory-trace.h` to your headers
2. add `./src/memory-trace.c(or .cc)` to your C or C++ compile list, you can also compile the source and load it via dynamic linking
3. add `set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -rdynamic")` to your CMakeLists.txt
4. call `print_trace()` to print current memory trace, the following content will be printed:
```shell
./samples/c/sample.c: 20L, 12 bytes
0   sample-c                            0x00000001014023d0 bar1 + 32
1   sample-c                            0x00000001014023e9 foo1 + 9
2   sample-c                            0x00000001014025b6 main + 326
3   libdyld.dylib                       0x00007fff69da7ed9 start + 1
4   ???                                 0x0000000000000001 0x0 + 1
./samples/c/sample.c: 27L, 48 bytes
0   sample-c                            0x0000000101402410 foo2 + 32
1   sample-c                            0x00000001014025c2 main + 338
2   libdyld.dylib                       0x00007fff69da7ed9 start + 1
3   ???                                 0x0000000000000001 0x0 + 1
./samples/c/sample.c: 31L, 32 bytes
0   sample-c                            0x0000000101402440 rab3 + 32
1   sample-c                            0x0000000101402459 bar3 + 9
2   sample-c                            0x0000000101402469 foo3 + 9
3   sample-c                            0x00000001014025ce main + 350
4   libdyld.dylib                       0x00007fff69da7ed9 start + 1
5   ???                                 0x0000000000000001 0x0 + 1
total: 92bytes
```
