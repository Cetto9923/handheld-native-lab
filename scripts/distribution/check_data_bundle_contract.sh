#!/usr/bin/env bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TASK_RUN="$(mktemp -d "$TASK_ROOT/.local/installer-contract.XXXXXX")"
mkdir -p "$TASK_RUN/classes"
javac --release 8 -d "$TASK_RUN/classes" \
  "$TASK_ROOT/src/android/app/java/org/ffrs/diagnostic/NativeDataBundle.java" \
  "$TASK_ROOT/tests/installer/NativeDataBundleContract.java"
java -cp "$TASK_RUN/classes" NativeDataBundleContract "$TASK_RUN/store"
printf 'evidence=%s\n' "$TASK_RUN"
