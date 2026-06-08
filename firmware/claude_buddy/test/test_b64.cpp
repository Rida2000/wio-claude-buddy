// Native unit test for the base64 shim. Build: see plan Task 3 step 2.
#include "../b64.h"
#include <cstring>
#include <cstdio>
#include <cassert>

int main() {
  unsigned char out[64];
  size_t n = 0;

  // "Man" -> "TWFu"
  assert(b64_decode((const unsigned char*)"TWFu", 4, out, sizeof(out), &n) == 0);
  assert(n == 3 && memcmp(out, "Man", 3) == 0);

  // one '=' pad: "Ma" -> "TWE="
  assert(b64_decode((const unsigned char*)"TWE=", 4, out, sizeof(out), &n) == 0);
  assert(n == 2 && memcmp(out, "Ma", 2) == 0);

  // two '=' pad: "M" -> "TQ=="
  assert(b64_decode((const unsigned char*)"TQ==", 4, out, sizeof(out), &n) == 0);
  assert(n == 1 && out[0] == 'M');

  // empty -> 0 bytes
  assert(b64_decode((const unsigned char*)"", 0, out, sizeof(out), &n) == 0);
  assert(n == 0);

  // output-too-small -> nonzero return
  assert(b64_decode((const unsigned char*)"TWFu", 4, out, 1, &n) != 0);

  // invalid char -> nonzero return
  assert(b64_decode((const unsigned char*)"@@@@", 4, out, sizeof(out), &n) != 0);

  printf("test_b64 OK\n");
  return 0;
}
