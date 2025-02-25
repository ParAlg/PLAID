#!/bin/bash

bazel build -c opt "//:speed_test"

executable="./bazel-bin/speed_test"
for type in 0 1 2; do
  echo "RNG type: $type" >> "result.txt"
  for i in {34..34}; do
    printf "%s," "${i}" >> "result.txt"
    for _ in {1..5}; do
      "${executable}" "sorting_in_memory" "$i" "$type" > "temp.txt"
      grep "DONE" "temp.txt" | sed -e "s/^.*DONE: \([0-9.]\+\)$/\1,/" | tr -d '\n' >> "result.txt"
    done
    echo "" >> "result.txt"
  done
done
cat "result.txt"
