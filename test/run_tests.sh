#!/bin/sh
# Host-side tests: decoder vs. C# reference (fuzz), and the page's homography solver.
set -e
cd "$(dirname "$0")"
sed 's/static const uint8_t KEY_TABLE\[320\]/const uint8_t KEY_TABLE_TEST[320]/; s/KEY_TABLE\[key_offset/KEY_TABLE_TEST[key_offset/' \
  ../src/guncon3_decode.c > /tmp/gc3_decode_copy.c
cc -O2 -fsanitize=address,undefined -o /tmp/gc3_test_decode test_decode.c /tmp/gc3_decode_copy.c
/tmp/gc3_test_decode
node test_homography.mjs
