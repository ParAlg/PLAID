#!/bin/bash

[ $# -ne 2 ] && echo "Usage ${0} start end" && exit 1

bazel build -c opt "//:sample_sort"

executable="./bazel-bin/sample_sort"
input_prefix="numbers"
output_prefix="result"
data_size="34"

for ssd_count in $(seq "$1" "$2"); do
  echo "Testing with ${ssd_count} random SSDs"
  bash scripts/clear.sh
  sudo bash scripts/fstrim.sh
  "${executable}" "--num_ssd=${ssd_count}" "--ssd_selection=r" gen "${data_size}" "${input_prefix}" 1 > "temp.txt" 2>&1
  ssd_numbers=$(grep "SSD numbers" "temp.txt" | sed -e "s/^.*numbers: \([0-9,]\+\)$/\1/" | tr -d '\n')
  printf "%s," "${ssd_count}" >> "result.txt"
  for _ in {1..3}; do
    bash scripts/reset.sh
    sudo bash scripts/fstrim.sh
    "${executable}" "--ssd=${ssd_numbers}" "run" "${input_prefix}" "${output_prefix}" > "temp.txt"
    grep "DONE" "temp.txt" | sed -e "s/^.*DONE: \([0-9.]\+\)$/\1,/" | tr -d '\n' >> "result.txt"
  done
  echo "" >> "result.txt"
done
rm "temp.txt"
cat "result.txt"
