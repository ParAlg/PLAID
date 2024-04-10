cc_binary(
    name = "sample_sort",
    srcs = ["sample-sort.cpp"],
    deps = [
        "//sorter:sample_sort",
        "//utils:random_number_generator",
    ],
)

cc_binary(
    name = "speed_test",
    srcs = ["speed-test.cpp"],
    deps = [
        "//utils:io_utils",
        "//utils:logger",
    ],
)

cc_library(
    name = "config",
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [],
)
