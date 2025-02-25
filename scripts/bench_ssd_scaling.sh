#!/bin/bash

bazel build -c opt "//:sample_sort"

executable="./bazel-bin/sample_sort"
input_prefix="numbers"
output_prefix="result"
data_size="34"

for type in 0 2 3; do
  echo "Type: $type" >> "result.txt"
  for ssd_count in 2 4 8 16 30; do
    echo "Testing with ${ssd_count} random SSDs"
    bash scripts/clear.sh
    sudo bash scripts/fstrim.sh
    "${executable}" "--num_ssd=${ssd_count}" "--ssd_selection=r" "--verbose=true" gen "${data_size}" "${input_prefix}" "$type" > "temp.txt" 2>&1
    ssd_numbers=$(grep "SSD numbers" "temp.txt" | sed -e "s/^.*numbers: \([0-9,]\+\)$/\1/" | tr -d '\n')
    echo "$ssd_numbers"
    printf "%s," "${ssd_count}" >> "result.txt"
    for _ in {1..3}; do
      bash scripts/reset.sh
      sudo bash scripts/fstrim.sh
      sleep 30
      "${executable}" "--ssd=${ssd_numbers}" "run" "${input_prefix}" "${output_prefix}" > "temp.txt"
      grep "DONE" "temp.txt" | sed -e "s/^.*DONE: \([0-9.]\+\)$/\1,/" | tr -d '\n' >> "result.txt"
    done
    echo "" >> "result.txt"
  done
done
rm "temp.txt"
cat "result.txt"
