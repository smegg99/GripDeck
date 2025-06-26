#!/bin/bash

set -e

if [ "$EUID" -ne 0 ]; then
  echo "You must run this script as root"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_NAME="gripdeck_battery"
MODULE_VERSION="1.0"

echo "Installing GripDeck Battery Driver..."

echo "Removing any previous installations..."
dkms remove -m ${MODULE_NAME} -v ${MODULE_VERSION} --all 2>/dev/null || true

echo "Copying module to DKMS tree..."
mkdir -p /usr/src/${MODULE_NAME}-${MODULE_VERSION}
cp -r "${SCRIPT_DIR}/"* /usr/src/${MODULE_NAME}-${MODULE_VERSION}/

echo "Registering with DKMS..."
dkms add -m ${MODULE_NAME} -v ${MODULE_VERSION}

echo "Building module..."
dkms build -m ${MODULE_NAME} -v ${MODULE_VERSION}

echo "Installing module..."
dkms install -m ${MODULE_NAME} -v ${MODULE_VERSION}

echo "Loading module..."
modprobe ${MODULE_NAME}

echo "Checking module status..."
if lsmod | grep -q ${MODULE_NAME}; then
  echo "GripDeck Battery Driver installed and loaded!"

  if grep -q "GripDeck HID battery driver loaded" <(sudo dmesg | grep gripdeck 2>/dev/null); then
    echo "GripDeck device detected and driver loaded successfully!"
  else
    echo "Driver loaded but no GripDeck device detected yet. Connect your device if not already connected."
  fi
else
  echo "Module failed to load. Check dmesg for errors"
fi

echo "Detailed logs at: /var/lib/dkms/${MODULE_NAME}/${MODULE_VERSION}/build/make.log"

echo ""
echo "To unload the driver, run: sudo modprobe -r ${MODULE_NAME}"
echo "To completely remove the driver, run: sudo dkms remove -m ${MODULE_NAME} -v ${MODULE_VERSION} --all"