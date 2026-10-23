#!/usr/bin/env bash
# Download only the official fixed NDK package into this worktree, retaining partial bytes.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DOWNLOADS="${ROOT}/.local/downloads"
LOGS="${ROOT}/.local/logs"
ARCHIVE="${DOWNLOADS}/android-ndk-r27d-darwin.zip"
URL='https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip'
EXPECTED_SIZE=839006233
EXPECTED_SHA1=2970926d705988f79baa9b04c51b4f7914dd8c56
DESTINATION="${ROOT}/.local/sdk/ndk/android-ndk-r27d"
mkdir -p "${DOWNLOADS}" "${LOGS}" "${ROOT}/.local/sdk/ndk"
if [[ -f "${DESTINATION}/source.properties" && -f "${DESTINATION}/build/cmake/android.toolchain.cmake" ]]; then
    printf 'Installed NDK: %s\n' "${DESTINATION}"
    exit 0
fi
size() { if [[ -f "${ARCHIVE}" ]]; then stat -f %z "${ARCHIVE}"; else printf '0\n'; fi; }
no_growth=0
for attempt in $(seq 1 256); do
    before="$(size)"
    if [[ "${before}" -eq "${EXPECTED_SIZE}" ]]; then break; fi
    if [[ "${before}" -gt "${EXPECTED_SIZE}" ]]; then
        echo 'Archive exceeds the official expected size; preserve it for investigation.' >&2
        exit 3
    fi
    result=0
    # A new curl invocation preserves bytes after a truncated HTTP response.
    # curl's internal retry can rewind them, so resumptions are explicit here.
    curl --fail --location --continue-at - --connect-timeout 20 --max-time 120 \
        --output "${ARCHIVE}" "${URL}" > "${LOGS}/ndk-transfer-${attempt}.log" 2>&1 || result=$?
    after="$(size)"
    printf 'attempt=%s before=%s after=%s curl_exit=%s\n' "${attempt}" "${before}" "${after}" "${result}" | tee -a "${LOGS}/ndk-transfer-summary.log"
    if [[ "${after}" -eq "${before}" ]]; then no_growth=$((no_growth + 1)); else no_growth=0; fi
    case "${result}" in 0|18|28|35|56) ;; *) exit "${result}" ;; esac
    if [[ "${no_growth}" -ge 4 ]]; then
        echo 'BLOCKED_NETWORK: four transfer attempts made no progress; partial archive preserved.' >&2
        exit 4
    fi
done
if [[ "$(size)" -ne "${EXPECTED_SIZE}" ]]; then exit 4; fi
printf '%s  %s\n' "${EXPECTED_SHA1}" "${ARCHIVE}" > "${LOGS}/ndk-official.sha1"
shasum -a 1 -c "${LOGS}/ndk-official.sha1" > "${LOGS}/ndk-checksum.log" 2>&1
cat "${LOGS}/ndk-checksum.log"
if [[ -e "${DESTINATION}" ]]; then
    echo "Refusing to overwrite incomplete NDK at ${DESTINATION}." >&2
    exit 3
fi
EXTRACT="$(mktemp -d "${ROOT}/.local/sdk/ndk/.extract.XXXXXX")"
unzip -q "${ARCHIVE}" -d "${EXTRACT}" > "${LOGS}/ndk-extract.log" 2>&1
mv "${EXTRACT}/android-ndk-r27d" "${DESTINATION}"
printf 'Installed NDK: %s\n' "${DESTINATION}"
