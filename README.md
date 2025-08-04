# PALID: Parallel Library for Asynchronous I/O Devices

This repository implements the external-memory primitives in [Scaling Parallel Algorithms to Massive Datasets using Multi-SSD Machines (SPAA 2025)](https://dl.acm.org/doi/10.1145/3694906.3743308).

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

## Reproducibility

Note that there can be reproducibility issues since `io_uring` is still under active development.

For example, we ran the following commands on the same virtual machine managed by KVM.
```shell
./bazel-bin/sample_sort gen 28 nums 1
./bazel-bin/sample_sort run nums result
```

The VM has the following hardware specs:
```
CPU: 30 virtual cores of a Ryzen 3950X
RAM: 16GB
```

We start with a fresh installation of Fedora KDE without any updates. It runs the following software:
```
Kernel: 6.11.4
gcc: 14.3.1
liburing: 2.6-2
```

The commands executed without error.

We then upgrade the machine to use the latest software provided by Fedora 41. We get
```
Kernel: 6.15.3
gcc: 14.3.1
liburing: 2.6-2
```
The program now segfaults at the sample step.

Turns out this is because there is not sufficient memory to be locked (i.e. marked as unswappable) by `io_uring`. Curiously, in both scenarios, `ulimit -l` is set to 8192. A kernel update likely increased the memory consumption per entry (due to new `io_uring` features) and resulted in what we observed.
