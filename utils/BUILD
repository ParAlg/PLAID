cc_library(
    name = "logger",
    srcs = ["logger.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:initialize",
    ],
)

cc_library(
    name = "file_utils",
    srcs = ["file_utils.cpp"],
    hdrs = ["file_utils.h"],
)

cc_library(
    name = "unordered_file_writer",
    srcs = ["unordered_file_writer.h"],
    deps = [
        ":file_utils",
        ":logger",
    ],
)

cc_library(
    name = "unordered_file_reader",
    srcs = ["unordered_file_reader.h"],
    deps = [":logger"],
)

cc_library(
    name = "io_utils",
    srcs = [],
    linkopts = ["-luring"],
    visibility = ["//visibility:public"],
    deps = [
        ":unordered_file_reader",
        ":unordered_file_writer",
    ],
)
