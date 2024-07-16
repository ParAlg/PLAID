#!/bin/bash

bazel build -c opt "//:speed_test"

executable="./bazel-bin/speed_test"
for i in {30..34}; do

  printf "%s," "${i}" >> "result.txt"
  for _ in {1..5}; do
    "${executable}" sorting "${i}" > "temp.txt"
    grep "DONE" "temp.txt" | sed -e "s/^.*DONE: \([0-9.]\+\)$/\1,/" | tr -d '\n' >> "result.txt"
  done
  echo "" >> "result.txt"
done
cat "result.txt"
