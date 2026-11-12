#!/usr/bin/env bash
# Host side check of the save editor: decrypt, patch, encrypt, decrypt again.
#
# The editor runs on the device, but its container handling is plain Java, so
# the round trip is checked here against a synthetic slot. The key comes from
# a generated 32 byte file on the classpath, the same place the app ships it.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${root}/.local/host-contract/save-cheater"
rm -rf "${out}"
mkdir -p "${out}/classes" "${out}/assets"

# A test key of its own: the host test never touches the shipped one.
python3 - "${out}/assets/ffrs_save_key.bin" <<'PY'
import sys
with open(sys.argv[1], "wb") as handle:
    handle.write(bytes((i * 7 + 3) & 0xff for i in range(32)))
PY

javac -nowarn -d "${out}/classes" \
    "${root}/tests/device/android/util/Log.java" \
    "${root}/src/android/app/java/org/ffrs/diagnostic/SaveCheater.java" \
    "${root}/tests/device/SaveCheaterRoundTrip.java"

java -cp "${out}/classes:${out}" org.ffrs.diagnostic.SaveCheaterRoundTrip
