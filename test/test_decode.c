// Compare the firmware decoder against a literal port of the C# reference (long arithmetic)
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
bool guncon3_decode(const uint8_t d[15], const uint8_t key[8], uint8_t out[13]);
extern const uint8_t guncon3_key[8];
extern const uint8_t KEY_TABLE_TEST[320];

int main(void) {
  srand(1234);
  int ok = 0, mismatch = 0, accepted = 0;
  for (int it = 0; it < 2000000; it++) {
    uint8_t d[15];
    for (int i = 0; i < 15; i++) d[i] = rand() & 0xFF;
    // reference (C#)
    long b_sum = d[13] ^ d[12];
    b_sum = b_sum + d[11] + d[10] - d[9] - d[8];
    b_sum = b_sum ^ d[7]; b_sum &= 0xFF;
    long a_sum = d[6] ^ b_sum;
    a_sum = a_sum - d[5] - d[4];
    a_sum ^= d[3];
    a_sum = a_sum + d[2] + d[1] - d[0];
    a_sum &= 0xFF;
    const uint8_t *key = guncon3_key;
    bool ref_ok = a_sum == key[7];
    uint8_t ref[13];
    if (ref_ok) {
      long ko = key[1] ^ key[2];
      ko = ko - key[3] - key[4];
      ko ^= key[5];
      ko = ko + key[6] - key[7];
      ko ^= d[14];
      ko = (ko + 0x26) & 0xFF;
      long ki = 4;
      for (long x = 12; x >= 0; x--) {
        long by = d[x];
        for (long y = 4; y > 1; y--) {
          ko--;
          long bkey = KEY_TABLE_TEST[ko + 0x41];
          long keyr = key[ki];
          if (--ki == 0) ki = 7;
          if ((bkey & 3) == 0) by = (by - bkey) - keyr;
          else if ((bkey & 3) == 1) by = (by + bkey) + keyr;
          else by = (by ^ bkey) ^ keyr;
        }
        ref[x] = (uint8_t) by;
      }
    }
    uint8_t out[13];
    bool fw_ok = guncon3_decode(d, key, out);
    if (fw_ok != ref_ok) { mismatch++; continue; }
    if (ref_ok) {
      accepted++;
      for (int i = 0; i < 13; i++) if (out[i] != ref[i]) { mismatch++; goto next; }
    }
    ok++;
    next:;
  }
  printf("ok=%d accepted=%d mismatch=%d\n", ok, accepted, mismatch);
  return mismatch != 0;
}
