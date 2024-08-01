#!/bin/bash

bazel build -c opt "//:sample_sort"

executable="./bazel-bin/sample_sort"
input_prefix="numbers"
output_prefix="result"
for i in {28..30}; do
  for _j in {1..3}; do
    bash scripts/clear.sh
    "${executable}" gen "${i}" "${input_prefix}" 0 > /dev/null 2>&1
    for _k in {1..3}; do
      bash scripts/reset.sh
      "${executable}" "run" "${input_prefix}" "${output_prefix}"
      verify_result=$("${executable}" "verify" "${output_prefix}" 0 "${i}" 2>&1)
      if [[ "${verify_result}" != *"No mismatch found after comparing"* ]]; then
        printf "Error for i=%s\n" "$i"
        exit 1
      fi
    done
  done
done
