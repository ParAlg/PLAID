This project is built with bazel. Just build everything with `bazel build -c opt //:all`

## Configs

All SSDs are assumed to reside in `/mnt/ssd%d`. This can be changed in `configs.h`, where `SSD_ROOT` and `SSD_COUNT` control the location and number of SSDs.

## Sample sort
```shell
./bazel-bin/samples_sort gen ...
./bazel-bin/samples_sort run ...
./bazel-bin/samples_sort verify ...
```

## Speed tests
```shell
./bazel-bin/speed_test <name of test>
```
