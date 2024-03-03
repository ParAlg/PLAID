cc_binary(
    name = "generate-random-numbers",
    srcs = ["generate-random-numbers.cc"],
    deps = [
        "config",
        "//utils:io_utils",
        "//utils:logger",
        "@parlaylib//parlay:parallel",
    ],
)

cc_library(
    name = "config",
    srcs = ["config.h"],
    deps = [],
)
