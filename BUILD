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
        "//utils:random_read",
    ],
)

cc_binary(
    name = "io_uring_test",
    srcs = ["io_uring_test.cpp"],
    linkopts = [
        "-luring",
    ],
    deps = [
        "//utils:logger",
        "@parlaylib//parlay/internal:get_time",
    ],
)

cc_library(
    name = "config",
    hdrs = ["configs.h"],
    visibility = ["//visibility:public"],
    deps = [],
)
