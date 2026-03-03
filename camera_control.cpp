#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <camera/camera.h>
#include <camera/device_discovery.h>
#include <camera/photography_settings.h>

#ifdef _WIN32
#include <io.h>
#define ACCESS_FUNC _access
#define F_OK 0
#else
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <cerrno>
#define ACCESS_FUNC access
#endif

// Tee streambuf: forwards every character to two streambufs (terminal + log file)
class TeeStreambuf : public std::streambuf {
public:
    TeeStreambuf(std::streambuf* primary, std::streambuf* log)
        : primary_(primary), log_(log) {}

protected:
    int_type overflow(int_type c) override {
        if (c == traits_type::eof())
            return c;
        const char ch = traits_type::to_char_type(c);
        if (primary_ && primary_->sputc(ch) == traits_type::eof())
            return traits_type::eof();
        if (log_ && log_->sputc(ch) == traits_type::eof())
            return traits_type::eof();
        return c;
    }

    int sync() override {
        int r = 0;
        if (primary_ && primary_->pubsync() != 0)
            r = -1;
        if (log_ && log_->pubsync() != 0)
            r = -1;
        return r;
    }

private:
    std::streambuf* primary_;
    std::streambuf* log_;
};

std::string getCurrentTime() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef WIN32
    if (localtime_s(&tm, &t))
#else
    if (localtime_r(&t, &tm))
#endif
    {
    }
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
    return std::string(buffer);
}

bool fileExists(const std::string& file_path) {
    return ACCESS_FUNC(file_path.c_str(), F_OK) == 0;
}

std::string getFileName(const std::string& path) {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash == std::string::npos) {
        return path;
    }
    return path.substr(lastSlash + 1);
}

class CameraController {
private:
    std::shared_ptr<ins_camera::Camera> camera_;
    bool is_connected_;

public:
    CameraController() : is_connected_(false) {}

    ~CameraController() {
        disconnect();
    }

    bool discoverAndConnect() {
        std::cout << "Discovering Insta360 cameras..." << std::endl;
        
        ins_camera::SetLogLevel(ins_camera::LogLevel::FATAL);
        ins_camera::DeviceDiscovery discovery;
        auto device_list = discovery.GetAvailableDevices();
        
        if (device_list.empty()) {
            std::cerr << "Error: No Insta360 camera found." << std::endl;
            std::cerr << "Please ensure:" << std::endl;
            std::cerr << "  1. Camera is powered on" << std::endl;
            std::cerr << "  2. Camera is connected via USB or WiFi" << std::endl;
            return false;
        }

        std::cout << "Found " << device_list.size() << " camera(s):" << std::endl;
        for (size_t i = 0; i < device_list.size(); i++) {
            const auto& device = device_list[i];
            std::cout << "  [" << i << "] " << device.camera_name 
                      << " (SN: " << device.serial_number 
                      << ", FW: " << device.fw_version << ")" << std::endl;
        }

        // Use the first available camera
        const auto& selected_device = device_list[0];
        std::cout << "\nConnecting to: " << selected_device.camera_name 
                  << " (SN: " << selected_device.serial_number << ")..." << std::endl;

        camera_ = std::make_shared<ins_camera::Camera>(selected_device.info);
        
        if (!camera_->Open()) {
            std::cerr << "Error: Failed to open camera connection." << std::endl;
            discovery.FreeDeviceDescriptors(device_list);
            return false;
        }

        // Sync time to camera
        time_t now = time(nullptr);
        std::tm tm{};
#ifdef WIN32
        localtime_s(&tm, &now);
        time_t time_seconds = _mkgmtime(&tm);
#else
        localtime_r(&now, &tm);
        time_t time_seconds = timegm(&tm);
#endif
        camera_->SyncLocalTimeToCamera(time_seconds);

        // X5 (and some other models) need longer than default 10s to respond to TakePhoto()
        // X5 at 72MP especially needs extra time; 60s accommodates mode switch + capture + response
        camera_->SetTimeout(60000);

        is_connected_ = true;
        std::cout << "Successfully connected to camera!" << std::endl;
        
        discovery.FreeDeviceDescriptors(device_list);
        return true;
    }

    void disconnect() {
        if (camera_ && is_connected_) {
            camera_->Close();
            is_connected_ = false;
            std::cout << "Disconnected from camera." << std::endl;
        }
    }

    bool takePhoto(const std::string& save_directory = "./") {
        if (!is_connected_ || !camera_) {
            std::cerr << "Error: Camera not connected." << std::endl;
            return false;
        }

        // Check if camera is still connected
        if (!camera_->IsConnected()) {
            std::cerr << "Error: Camera connection lost." << std::endl;
            is_connected_ = false;
            return false;
        }

        std::cout << "Setting photo mode..." << std::endl;
        bool ret = camera_->SetPhotoSubMode(ins_camera::SubPhotoMode::PHOTO_SINGLE);
        if (!ret) {
            std::cerr << "Warning: Failed to set photo mode, continuing anyway..." << std::endl;
        }

        // X5 needs time to switch from video to photo mode before TakePhoto
        std::cout << "Waiting for camera to switch to photo mode..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));

        std::cout << "Taking photo..." << std::endl;
        const auto url = camera_->TakePhoto();
        
        if (url.Empty() || !url.IsSingleOrigin()) {
            std::cerr << "Error: Failed to take photo." << std::endl;
            return false;
        }

        const std::string photo_url = url.GetSingleOrigin();
        std::cout << "Photo captured! URL: " << photo_url << std::endl;

        // Download the photo if save directory is provided
        if (!save_directory.empty()) {
            std::string save_path = save_directory;
            if (save_path.back() != '/' && save_path.back() != '\\') {
                save_path += "/";
            }
            
            // Check if directory exists
            if (!fileExists(save_path)) {
                std::cerr << "Warning: Save directory does not exist: " << save_path << std::endl;
                std::cerr << "Photo URL saved on camera: " << photo_url << std::endl;
                return true;
            }

            std::string file_name = getFileName(photo_url);
            if (file_name.empty()) {
                file_name = "photo_" + getCurrentTime() + ".jpg";
            }
            
            std::string full_path = save_path + file_name;
            std::cout << "Downloading photo to: " << full_path << std::endl;

            int64_t last_progress = -1;
            bool download_success = camera_->DownloadCameraFile(photo_url, full_path,
                [&](int64_t current, int64_t total_size) {
                    int64_t progress = total_size > 0 ? (current * 100 / total_size) : 0;
                    if (progress != last_progress) {
                        std::cout << "\rDownload progress: " << progress << "%" << std::flush;
                        last_progress = progress;
                    }
                });
            
            std::cout << std::endl;
            
            if (download_success) {
                std::cout << "Photo successfully downloaded to: " << full_path << std::endl;
                return true;
            } else {
                std::cerr << "Error: Failed to download photo." << std::endl;
                std::cerr << "Photo URL on camera: " << photo_url << std::endl;
                return false;
            }
        }

        return true;
    }

    bool shutdownCamera() {
        if (!is_connected_ || !camera_) {
            std::cerr << "Error: Camera not connected." << std::endl;
            return false;
        }

        std::cout << "Shutting down camera..." << std::endl;
        bool ret = camera_->ShutdownCamera();
        
        if (ret) {
            std::cout << "Camera shutdown command sent successfully." << std::endl;
            is_connected_ = false;
            return true;
        } else {
            std::cerr << "Error: Failed to shutdown camera." << std::endl;
            return false;
        }
    }

    bool getBatteryStatus() {
        if (!is_connected_ || !camera_) {
            std::cerr << "Error: Camera not connected." << std::endl;
            return false;
        }

        ins_camera::BatteryStatus status{};
        bool ret = camera_->GetBatteryStatus(status);
        
        if (!ret) {
            std::cerr << "Error: Failed to get battery status." << std::endl;
            return false;
        }

        std::cout << "Battery Status:" << std::endl;
        std::cout << "  Power Type: " << (status.power_type == ins_camera::PowerType::BATTERY ? "Battery" : "Adapter") << std::endl;
        std::cout << "  Battery Level: " << status.battery_level << "%" << std::endl;
        std::cout << "  Battery Scale: " << status.battery_scale << std::endl;
        
        return true;
    }

    bool isConnected() const {
        return is_connected_ && camera_ && camera_->IsConnected();
    }
};

void printUsage(const char* program_name) {
    std::cout << "Insta360 Camera Control for Raspberry Pi" << std::endl;
    std::cout << "Usage: " << program_name << " <command> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  connect              - Connect to camera" << std::endl;
    std::cout << "  photo [save_dir]    - Take a photo (optionally save to directory)" << std::endl;
    std::cout << "  shutdown             - Power off the camera" << std::endl;
    std::cout << "  battery              - Get battery status" << std::endl;
    std::cout << "  interactive          - Interactive mode" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " photo                    # Take photo" << std::endl;
    std::cout << "  " << program_name << " photo ./photos          # Take photo and save to ./photos" << std::endl;
    std::cout << "  " << program_name << " shutdown                # Power off camera" << std::endl;
    std::cout << "  " << program_name << " interactive             # Interactive mode" << std::endl;
}

int main(int argc, char* argv[]) {
    std::ofstream log_file;
    const char* home = std::getenv("HOME");
    const std::string log_dir = home ? std::string(home) + "/log" : "log";
#ifndef _WIN32
    mkdir(log_dir.c_str(), 0755);
#endif
    const std::string log_path = log_dir + "/camera_control_" + getCurrentTime() + ".log";
    log_file.open(log_path, std::ios::out | std::ios::trunc);

    std::streambuf* orig_cout = std::cout.rdbuf();
    std::streambuf* orig_cerr = std::cerr.rdbuf();
    TeeStreambuf tee_cout(orig_cout, log_file.is_open() ? log_file.rdbuf() : nullptr);
    TeeStreambuf tee_cerr(orig_cerr, log_file.is_open() ? log_file.rdbuf() : nullptr);

    struct RestoreRdbuf {
        std::streambuf* c;
        std::streambuf* e;
        ~RestoreRdbuf() {
            std::cout.flush();
            std::cerr.flush();
            std::cout.rdbuf(c);
            std::cerr.rdbuf(e);
        }
    } guard = { orig_cout, orig_cerr };

    if (log_file.is_open()) {
        std::cout.rdbuf(&tee_cout);
        std::cerr.rdbuf(&tee_cerr);
    }

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    
    CameraController controller;

    if (command == "connect") {
        if (!controller.discoverAndConnect()) {
            return 1;
        }
        std::cout << "Camera connected. Use 'photo', 'shutdown', or 'battery' commands." << std::endl;
        return 0;
    }

    // For other commands, we need to connect first
    if (!controller.discoverAndConnect()) {
        return 1;
    }

    if (command == "photo") {
        std::string save_dir = (argc > 2) ? argv[2] : "./";
        bool success = controller.takePhoto(save_dir);
        controller.disconnect();
        return success ? 0 : 1;
    }
    else if (command == "shutdown") {
        bool success = controller.shutdownCamera();
        controller.disconnect();
        return success ? 0 : 1;
    }
    else if (command == "battery") {
        bool success = controller.getBatteryStatus();
        controller.disconnect();
        return success ? 0 : 1;
    }
    else if (command == "interactive") {
        std::cout << "\n=== Interactive Mode ===" << std::endl;
        std::cout << "Commands: photo [dir], shutdown, battery, quit" << std::endl;
        
        std::string line;
        while (true) {
            std::cout << "\n> ";
            std::getline(std::cin, line);
            
            if (line == "quit" || line == "exit") {
                break;
            }
            else if (line == "photo") {
                controller.takePhoto("./");
            }
            else if (line.substr(0, 5) == "photo") {
                std::string dir = line.length() > 6 ? line.substr(6) : "./";
                controller.takePhoto(dir);
            }
            else if (line == "shutdown") {
                if (controller.shutdownCamera()) {
                    break;
                }
            }
            else if (line == "battery") {
                controller.getBatteryStatus();
            }
            else if (line.empty()) {
                continue;
            }
            else {
                std::cout << "Unknown command. Try: photo, shutdown, battery, quit" << std::endl;
            }
            
            // Check if still connected
            if (!controller.isConnected()) {
                std::cout << "Camera disconnected. Exiting..." << std::endl;
                break;
            }
        }
        
        controller.disconnect();
        return 0;
    }
    else {
        std::cerr << "Unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}

