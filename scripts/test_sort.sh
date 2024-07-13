#!/bin/bash

bazel build -c opt "//:sample_sort"

executable="./bazel-bin/sample_sort"
input_prefix="numbers"
output_prefix="result"
for i in {30..40}; do
  bash scripts/clear.sh
  "${executable}" gen "${i}" "${input_prefix}" 1
  printf "%s," "${i}" >> "result.txt"
  for _ in {1..5}; do
    bash scripts/reset.sh
    "${executable}" "run" "${input_prefix}" "${output_prefix}" > "temp.txt"
    grep "DONE" "temp.txt" | sed -e "s/^.*DONE: \([0-9.]\+\)$/\1,/" | tr -d '\n' >> "result.txt"
  done
done
rm "temp.txt"
cat "result.txt"
