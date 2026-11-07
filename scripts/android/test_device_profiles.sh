#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${root}/.local/honor-device-profiles/host-contract"
mkdir -p "${out}"
javac -d "${out}" "${root}/src/android/app/java/org/ffrs/diagnostic/DeviceProfile.java" "${root}/tests/device/DeviceProfileContract.java"
java -cp "${out}" org.ffrs.diagnostic.DeviceProfileContract
