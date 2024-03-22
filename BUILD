cc_binary(
    name = "sample-sort",
    srcs = ["sample-sort.cpp"],
    deps = ["//sorter:sample_sort"],
)

cc_binary(
    name = "generate-random-numbers",
    srcs = ["generate-random-numbers.cc"],
    deps = [
        "//utils:logger",
        "//utils:random_number_generator",
    ],
)

cc_library(
    name = "config",
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [],
)

cc_binary(
    name = "test-rng",
    srcs = ["test-rng.cpp"],
    deps = [
        "//utils:logger",
        "//utils:random_number_generator",
    ],
)
