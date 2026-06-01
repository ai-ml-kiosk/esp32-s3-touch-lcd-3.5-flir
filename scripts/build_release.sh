#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${1:-waveshare-esp32-s3-touch-lcd-35b-flir}"
BUILD_DIR=".pio/build/${ENV_NAME}"
DIST_DIR="dist"

mkdir -p "${DIST_DIR}"

env \
  -u HTTP_PROXY \
  -u HTTPS_PROXY \
  -u http_proxy \
  -u https_proxy \
  -u ALL_PROXY \
  -u all_proxy \
  pio run -e "${ENV_NAME}"

cp "${BUILD_DIR}/firmware.bin" "${DIST_DIR}/${ENV_NAME}-firmware.bin"
cp "${BUILD_DIR}/firmware.factory.bin" "${DIST_DIR}/${ENV_NAME}-firmware.factory.bin"

cat > "${DIST_DIR}/README-flash.md" <<EOF
# ESP32-S3 Touch LCD 3.5B FLIR Firmware

## Recommended Full Restore Flash

\`\`\`bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1301 --baud 921600 write_flash 0x0 ${ENV_NAME}-firmware.factory.bin
\`\`\`

## PlatformIO Upload From Source

\`\`\`bash
pio run --target upload --upload-port /dev/cu.usbmodem1301
\`\`\`

## Assets

- \`${ENV_NAME}-firmware.bin\`: application firmware image.
- \`${ENV_NAME}-firmware.factory.bin\`: combined image with bootloader, partition table, boot app, and firmware.
EOF

echo "Release artifacts written to ${DIST_DIR}/"
