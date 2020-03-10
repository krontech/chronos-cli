#!/bin/bash
# The user may provide us with a firmware file to load, otherwise find the
# latest and greatest PMIC firmware in the /lib/firmware/ directory.
if [ $# -lt 1 ]; then
    FIRMWARE_FILE=$(find /lib/firmware/ -name 'chronos-pmic-*.hex' | sort | tail -1)
else
    FIRMWARE_FILE=$1
fi
if [ ! -e "${FIRMWARE_FILE}" ] || [ ! -r "${FIRMWARE_FILE}" ]; then
    echo "Firmware file \'$1\' is not a readable file."
    exit 1    
fi
FIRMWARE_VERSION=$(echo ${FIRMWARE_FILE} | sed -nr 's/.*[^0-9]([0-9]+).hex$/\1/p')

# Test if the PMIC is in bootloader mode, if so we are in some kind of recovery state.
BOOTLOAD=$(/usr/bin/cam-pcUtil -i | grep 'application')
if [ -z "${BOOTLOAD}" ]; then
    echo "PMIC is not in application mode, exiting..."
    exit
fi
# Get the PMIC firmware version, don't attempt an upgrade to the same version.
CURRENT_VERSION=$(/usr/bin/cam-pcUtil -v | sed -nr 's/.*Version: ([0-9]+)[^0-9]*/\1/p')
echo "PMIC firmware version: running=v${CURRENT_VERSION} latest=v${FIRMWARE_VERSION}"
if [ $# -lt 1 ] && [ "${CURRENT_VERSION}" -ge "${FIRMWARE_VERSION}" ]; then
    echo "PMIC firmware up to date, not upgrading."
    exit 0
fi

# Upgrade the PMIC firmware version.
if /usr/bin/cam-pcUtil -u ${FIRMWARE_FILE}; then
    echo "PMIC firmware upgraded successfully."
    exit 0
else
    echo "PMIC firmware upgrade failed."
    exit 1
fi

