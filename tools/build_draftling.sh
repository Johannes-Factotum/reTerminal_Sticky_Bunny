#!/usr/bin/env bash
# Build Draftling (https://github.com/clackups/draftling, MIT) for the
# Sticky's second app slot, with the "Back to Bunny" patch applied.
#
# Needs an ESP-IDF v6.1+ environment (". $IDF_PATH/export.sh").
#
#   tools/build_draftling.sh                 # build only
#   tools/build_draftling.sh /dev/ttyUSB0    # build and flash to ota_1
#
# Output: build/draftling/draftling.bin (flash offset 0x800000).
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly REV="53f300cd5f58d4dbf64512614a9702eb7438a214"
readonly SRC="${DRAFTLING_SRC:-${ROOT}/build/draftling-src}"
readonly OUT="${ROOT}/build/draftling"
readonly PATCH="${ROOT}/third_party/draftling/0001-sticky-bunny-dual-boot.patch"
readonly PORT="${1:-}"

command -v idf.py >/dev/null || {
    echo "idf.py not found: source ESP-IDF v6.1+ (export.sh) first" >&2
    exit 1
}

if [[ ! -d "${SRC}/.git" ]]; then
    git clone https://github.com/clackups/draftling "${SRC}"
fi
git -C "${SRC}" fetch --quiet origin
git -C "${SRC}" checkout --quiet --force "${REV}"
git -C "${SRC}" clean --quiet -fd firmware/main firmware/components/editor
git -C "${SRC}" apply "${PATCH}"
# Same partition table as Sticky Bunny so both firmwares agree on slots.
cp "${ROOT}/partitions.csv" \
   "${SRC}/firmware/partitions_seeed_reterminal_sticky.csv"

(cd "${SRC}/firmware" && idf.py --preset seeed_reterminal_sticky build)

mkdir -p "${OUT}"
cp "${SRC}/firmware/build/seeed_reterminal_sticky/draftling.bin" \
   "${OUT}/draftling.bin"
echo "Built ${OUT}/draftling.bin"

if [[ -n "${PORT}" ]]; then
    python -m esptool --chip esp32s3 -p "${PORT}" -b 921600 \
        write_flash 0x800000 "${OUT}/draftling.bin"
fi
