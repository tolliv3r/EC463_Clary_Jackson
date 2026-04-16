# Insta360 Camera Control for Raspberry Pi

This application provides basic control of Insta360 cameras from a Raspberry Pi Zero 2 W, including:
- Taking photos
- Powering the camera on/off (shutdown)
- Checking battery status
- Interactive mode for multiple commands

## Prerequisites

### Hardware
- Raspberry Pi
- Insta360 camera (X4, X5, or compatible model)
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
./camera_control photo /example/folder/

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

### WiFi Connection (WIP)
1. Put camera in WiFi mode
2. Connect Raspberry Pi to camera's WiFi network
3. Run the application

The application will automatically discover cameras connected via USB or WiFi.

## Installation (optional)

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

enable photo timelapse script:
```
sudo systemctl enable --now polar-listener.service
```

disable:
```
sudo systemctl disable --now polar-listener.service
systemctl status polar-listener.service
```

if looping photos or similar issues persist:
```
ps aux | grep -E 'polar_listener|camera_control' | grep -v grep
```

other similar useful commands:
```
sudo systemctl start  polar-listener.service    # start once, don't enable at boot
sudo systemctl stop   polar-listener.service    # stop without un-disabling
sudo systemctl enable polar-listener.service    # enable at boot without starting now
sudo journalctl -u polar-listener.service -f    # follow live logs
```


## License

This application uses the Insta360 Camera SDK. Please refer to Insta360's SDK license terms.