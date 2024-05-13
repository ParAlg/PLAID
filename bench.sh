#!/usr/bin/zsh
for size in 27 30 32 34; do
  echo "Testing with array of size 2^${size}"
  ./bazel-bin/sample_sort "$size" true
  (cd /mnt && ./clean.sh)
done
