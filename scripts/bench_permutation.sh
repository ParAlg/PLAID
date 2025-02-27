#!/usr/bin/bash

# In-memory

bazel build -c opt "//:all"
bin="./bazel-bin"

for data_size in $(seq 29 34); do
  printf "%s," "$data_size" >> "result.txt"
  for _ in $(seq 1 5); do
    "$bin/speed_test" "permutation_in_memory" "$data_size" > "temp.txt" 2>&1
    grep "DONE" "temp.txt" | sed -e "s/^.*DONE: \([0-9.]\+\)$/\1,/" | tr -d '\n' >> "result.txt"
  done
  echo "" >> "result.txt"
done
rm "temp.txt"
cat "result.txt"

