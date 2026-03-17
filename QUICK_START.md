# Quick Start Guide

## On Raspberry Pi

### 1. Copy Files to Raspberry Pi
```bash
# Copy the entire insta360_control directory to your Pi
scp -r insta360_control pi@raspberrypi.local:~/
```

### 2. Run Setup Script
```bash
cd ~/insta360_control
./setup.sh
```

This will:
- Install dependencies (g++, make)
- Build the application
- Configure library paths

### 3. Use the Application

#### Basic Commands
```bash
# Take a photo (saves to current directory)
./camera_control photo

# Take a photo and save to specific directory
./camera_control photo ~/photos

# Check battery status
./camera_control battery

# Power off camera
./camera_control shutdown

# Interactive mode
./camera_control interactive
```

#### Using the Helper Script
```bash
# Use run.sh to automatically set library path
./run.sh photo
./run.sh shutdown
./run.sh battery
./run.sh interactive
```

## Camera Connection

### USB Connection
1. Connect camera to Pi via USB
2. Power on camera
3. Run commands

### WiFi Connection
1. Put camera in WiFi mode
2. Connect Pi to camera's WiFi network
3. Run commands

## Troubleshooting

### Library Path Issues
If you get "library not found" errors:
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/CameraSDK-20250418_161512-2.0.2-gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/lib
```

Or use the helper script:
```bash
./run.sh photo
```

### USB Permissions
If USB connection fails, try:
```bash
sudo ./camera_control photo
```

### Camera Not Found
- Ensure camera is powered on
- Check USB/WiFi connection
- Try disconnecting and reconnecting

## Example Workflow

```bash
# 1. Connect camera and check battery
./camera_control battery

# 2. Take some photos
./camera_control photo ~/photos
./camera_control photo ~/photos

# 3. Shutdown camera when done
./camera_control shutdown
```

## Interactive Mode Example

```bash
./camera_control interactive
> battery
> photo ~/photos
> photo ~/photos
> shutdown
> quit
```

### SSH to Pi

```
ssh dev@192.168.1.15
polar
```