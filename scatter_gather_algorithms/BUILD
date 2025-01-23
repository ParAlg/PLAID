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
        "@com_google_absl//absl/container:btree",
        "@parlaylib//parlay:primitives",
        "@parlaylib//parlay/internal:get_time",
    ],
)

cc_library(
    name = "permutation",
    srcs = ["permutation.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scatter_gather",
        "@parlaylib//parlay:primitives",
        "@parlaylib//parlay/internal:get_time",
    ],
)

cc_library(
    name = "scatter_gather",
    srcs = ["scatter_gather.h"],
    deps = [
        "//:config",
        "//utils:aligned_type_allocator",
        "//utils:io_utils",
        "//utils:logger",
        "@parlaylib//parlay:primitives",
        "@parlaylib//parlay/internal:get_time",
    ],
)

cc_library(
    name = "scatter_gather_nop",
    srcs = ["nop.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":scatter_gather",
        "//utils:io_utils",
    ],
)
