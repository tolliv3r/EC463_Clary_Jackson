#!/bin/bash
# Helper script to power off the camera with camera_control

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"
SDK_DIR="$ROOT_DIR/CameraSDK-20250418_161512-2.0.2-gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu"
LIB_DIR="$SDK_DIR/lib"
APP="$ROOT_DIR/camera_control"

# check if application exists
if [ ! -f "$APP" ]; then
    echo "Error: camera_control not found."
    exit 1
fi

# check if library directory exists
if [ ! -d "$LIB_DIR" ]; then
    echo "Error: SDK library directory not found: $LIB_DIR"
    exit 1
fi

# run shutdown command
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$LIB_DIR
exec "$APP" shutdown "$@"
