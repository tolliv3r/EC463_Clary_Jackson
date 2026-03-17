#!/bin/bash
# Setup script for Insta360 Camera Control on Raspberry Pi

set -e

echo "Insta360 Camera Control - Setup Script"
echo "======================================"
echo ""

# Check if running on Raspberry Pi (optional check)
if [ ! -f /proc/device-tree/model ]; then
    echo "Warning: This may not be a Raspberry Pi. Continuing anyway..."
fi

# Check for required tools
echo "Checking dependencies..."
if ! command -v g++ &> /dev/null; then
    echo "g++ not found. Installing build-essential..."
    sudo apt-get update
    sudo apt-get install -y build-essential g++ make
else
    echo "✓ g++ found"
fi

if ! command -v make &> /dev/null; then
    echo "make not found. Installing..."
    sudo apt-get install -y make
else
    echo "✓ make found"
fi

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Project root is the parent of the helper directory
ROOT_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"
SDK_DIR="$ROOT_DIR/CameraSDK-20250418_161512-2.0.2-gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu"
LIB_DIR="$SDK_DIR/lib"

# Check if SDK is extracted
if [ ! -d "$SDK_DIR" ]; then
    echo ""
    echo "Error: SDK directory not found: $SDK_DIR"
    echo "Please ensure the SDK tar.gz file is extracted."
    exit 1
fi

echo ""
echo "✓ SDK directory found"

# Build the application
echo ""
echo "Building application..."
cd "$ROOT_DIR"
make clean
make

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ Build successful!"
else
    echo ""
    echo "✗ Build failed!"
    exit 1
fi

# Set up library path
echo ""
echo "Setting up library path..."

LIB_PATH_LINE="export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:$LIB_DIR"

# Check if already in .bashrc
if grep -q "$LIB_DIR" ~/.bashrc 2>/dev/null; then
    echo "✓ Library path already configured in ~/.bashrc"
else
    echo "Adding library path to ~/.bashrc..."
    echo "" >> ~/.bashrc
    echo "# Insta360 Camera SDK library path" >> ~/.bashrc
    echo "$LIB_PATH_LINE" >> ~/.bashrc
    echo "✓ Library path added to ~/.bashrc"
fi

# Export for current session
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$LIB_DIR

echo ""
echo "Setup complete!"
echo ""
echo "To use the application:"
echo "  1. Source your bashrc: source ~/.bashrc"
echo "  2. Run: ./camera_control <command>"
echo ""
echo "Or run directly:"
echo "  LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:$LIB_DIR ./camera_control photo"
echo ""

