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
  firmware-c/vendor/cJSON.c firmware-c/stall_estimator.c -lm -o build/native-tests/test-protocol
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize="$sanitize" \
  -Ifirmware-c -Itests/native tests/native/test_settings.c -lm -o build/native-tests/test-settings
build/native-tests/test-profile
build/native-tests/test-protocol
build/native-tests/test-settings
"$compiler" -std=c11 -Wall -Wextra -Werror -fsanitize="$sanitize" -Ifirmware-c \
  firmware-c/current_stats.c tests/native/test_current.c -o build/native-tests/test-current
build/native-tests/test-current

"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DCJSON_NESTING_LIMIT=8 -DMOTOR_BOARD_DRV8452=1 -DNEMA23_HOST_TEST=1 -fsanitize="$sanitize" \
  -Ifirmware-c -Itests/native tests/native/test_nema23.c firmware-c/nema23.c firmware-c/profile.c firmware-c/vendor/cJSON.c -lm -o build/native-tests/test-nema23
build/native-tests/test-nema23

"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DCJSON_NESTING_LIMIT=8 -DMOTOR_BOARD_DRV8847=1 -fsanitize="$sanitize" \
  -Ifirmware-c -Itests/native tests/native/test_drv8847_protocol.c firmware-c/profile.c firmware-c/vendor/cJSON.c -lm -o build/native-tests/test-drv8847-protocol
build/native-tests/test-drv8847-protocol build/native-tests/drv8847-telemetry.json
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DMOTOR_BOARD_DRV8847=1 -fsanitize="$sanitize" \
  -Ifirmware-c -Itests/native tests/native/test_drv8847_motor.c firmware-c/profile.c -lm -o build/native-tests/test-drv8847-motor
build/native-tests/test-drv8847-motor

"$compiler" -std=c11 -Wall -Wextra -Werror -fsanitize="$sanitize" -Ifirmware-c tests/native/test_stall_estimator.c firmware-c/stall_estimator.c -lm -o build/native-tests/test-stall-estimator
build/native-tests/test-stall-estimator
"$compiler" -std=c11 -Wall -Wextra -Werror -fsanitize="$sanitize" -Ifirmware-c \
  tests/native/test_stall_response.c -o build/native-tests/test-stall-response
build/native-tests/test-stall-response
