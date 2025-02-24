#! /usr/bin/bash

bazel build -c opt "//:speed_test"

executable="./bazel-bin/speed_test"
workload="scatter_gather_nop"

for i in {30..40}; do
  "${executable}" "$workload" 37 "$num_buckets" > "temp.txt" 2>&1
  printf "%s," "$num_buckets" >> "result.txt"
  grep "Throughput" "temp.txt" | sed -e "s/^.*Throughput: \([0-9.]\+\)$/\1,/" >> "result.txt"
done
rm "temp.txt"
cat "result.txt"