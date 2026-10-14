#!/bin/sh
set -eu
export UBSAN_OPTIONS=halt_on_error=1
cd "$(dirname "$0")/.."
mkdir -p build/native-tests
compiler="${CC:-cc}"
# UBSan is supported on both macOS and Linux. CI also runs ASan on Linux.
sanitize="${NATIVE_SANITIZERS:-undefined}"
"$compiler" -std=c11 -Wall -Wextra -Werror -fsanitize="$sanitize" -Ifirmware-c \
  firmware-c/profile.c tests/native/test_profile.c -lm -o build/native-tests/test-profile
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DCJSON_NESTING_LIMIT=8 -fsanitize="$sanitize" \
  -Ifirmware-c -Itests/native tests/native/test_protocol.c firmware-c/profile.c \
  firmware-c/vendor/cJSON.c -lm -o build/native-tests/test-protocol
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize="$sanitize" \
  -Ifirmware-c -Itests/native tests/native/test_settings.c -lm -o build/native-tests/test-settings
build/native-tests/test-profile
build/native-tests/test-protocol
build/native-tests/test-settings
