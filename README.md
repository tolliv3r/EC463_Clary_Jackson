# Insta360 Camera Control for Raspberry Pi Zero 2 W

This application provides basic control of Insta360 cameras from a Raspberry Pi Zero 2 W, including:
- Taking photos
- Powering the camera on/off (shutdown)
- Checking battery status
- Interactive mode for multiple commands

## Prerequisites

### Hardware
- Raspberry Pi Zero 2 W (or compatible Raspberry Pi)
- Insta360 camera (X3, X4, X5, or compatible model)
- USB cable or WiFi connection to camera

### Software
- Raspberry Pi OS (or compatible Linux distribution)
- C++ compiler (g++)
- Make

Install dependencies:
```bash
sudo apt-get update
sudo apt-get install build-essential g++ make
```

## Building

1. Extract the SDK if not already done:
```bash
tar -xzf ../CameraSDK-2.0.2-build1-20250418-gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu.tar.gz
```

2. Build the application:
```bash
make
```

3. Set up library path:
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/CameraSDK-20250418_161512-2.0.2-gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/lib
```

Or add to `~/.bashrc`:
```bash
echo 'export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/insta360_control/CameraSDK-20250418_161512-2.0.2-gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/lib' >> ~/.bashrc
source ~/.bashrc
```

## Usage

### Command Line Interface

#### Take a photo
```bash
./camera_control photo
```

#### Take a photo and save to directory
```bash
./camera_control photo ./photos
```

#### Power off the camera
```bash
./camera_control shutdown
```

#### Check battery status
```bash
./camera_control battery
```

#### Interactive mode
```bash
./camera_control interactive
```

In interactive mode, you can run multiple commands:
- `photo [directory]` - Take a photo
- `shutdown` - Power off camera
- `battery` - Check battery status
- `quit` or `exit` - Exit interactive mode

### Examples

```bash
# Connect and take a photo
./camera_control photo

# Take photo and save to specific directory
./camera_control photo /home/pi/photos

# Check battery before taking photos
./camera_control battery

# Shutdown camera after use
./camera_control shutdown

# Interactive session
./camera_control interactive
> photo ./photos
> battery
> shutdown
```

## Camera Connection

### USB Connection
1. Connect camera to Raspberry Pi via USB cable
2. Ensure camera is powered on
3. Run the application

### WiFi Connection
1. Put camera in WiFi mode
2. Connect Raspberry Pi to camera's WiFi network
3. Run the application

The application will automatically discover cameras connected via USB or WiFi.

## Troubleshooting

### "No camera found"
- Ensure camera is powered on
- Check USB connection (if using USB)
- Check WiFi connection (if using WiFi)
- Try running with `sudo` (may need USB permissions)

### "Failed to open camera"
- Camera may be in use by another application
- Try disconnecting and reconnecting
- Check camera battery level

### Library not found
- Ensure `LD_LIBRARY_PATH` is set correctly
- Check that `libCameraSDK.so` exists in the lib directory
- Try installing the library system-wide (see Installation)

## Installation (Optional)

To install system-wide:
```bash
sudo make install
```

This will:
- Copy `camera_control` to `/usr/local/bin`
- Copy `libCameraSDK.so` to `/usr/local/lib`
- Run `ldconfig` to update library cache

After installation, you can run from anywhere:
```bash
camera_control photo
```

## Files

- `camera_control.cpp` - Main application source code
- `Makefile` - Build configuration
- `CameraSDK-*/` - Insta360 Camera SDK (headers, library, examples)

## Notes

- **Power On**: The SDK does not provide a software "power on" function. The camera must be manually powered on or connected via USB/WiFi.
- **Power Off**: The `shutdown` command will power off the camera.
- **Photo Download**: Photos are automatically downloaded after capture if a save directory is specified.
- **Connection**: The camera must be powered on and connected (USB or WiFi) before running commands.

## License

This application uses the Insta360 Camera SDK. Please refer to Insta360's SDK license terms.
