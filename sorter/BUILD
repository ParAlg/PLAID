cc_library(
    name = "sample_sort",
    srcs = ["sample_sort.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scatter_gather",
        "//:config",
        "//utils:io_utils",
        "//utils:logger",
        "//utils:random_read",
        "@parlaylib//parlay:primitives",
        "@parlaylib//parlay/internal:get_time",
    ],
)

cc_library(
    name = "scatter_gather",
    srcs = ["scatter_gather.h"],
    deps = [
        "//:config",
        "//utils:io_utils",
        "//utils:logger",
        "@parlaylib//parlay:primitives",
        "@parlaylib//parlay/internal:get_time",
    ],
)
