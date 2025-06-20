# PALID: Parallel Library for Asynchronous I/O Devices

This project is built with bazel. Just build everything with `bazel build -c opt //:all`

CMake support is planned but not implemented yet.

## Setup 

All SSDs are assumed to reside in `/mnt/ssd%d`. This can be changed in `configs.h`, where `SSD_ROOT` and `SSD_COUNT` control the location and number of SSDs. These need to be adjusted so that the program can function properly. On devices without a multidisk setup, simply create a directory `/mnt/ssd0` and set `SSD_COUNT` to 1.

## Running sample sort
Run the following commands.

```shell
# Generate a random permutation of numbers from 0 to 2^28 - 1 and shard
# the result in files prefixed with nums.
./bazel-bin/sample_sort gen 28 nums 1
# Run sample sort on nums and store the result in files prefixed with result.
./bazel-bin/sample_sort run nums result
# Verify that files prefixed with result do contain the correct sorted data.
./bazel-bin/sample_sort verify result 0 28
```

## Speed tests
```shell
./bazel-bin/speed_test <name of test>
```
