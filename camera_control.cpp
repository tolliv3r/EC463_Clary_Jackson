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

// #region agent log
static void debugLog(const char* hyp, const char* loc, const char* msg, const std::string& data = "") {
    FILE* f = fopen("/tmp/debug-1daab7.log", "a");
    if (f) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        fprintf(f, "{\"sessionId\":\"1daab7\",\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\",\"data\":\"%s\",\"timestamp\":%lld}\n",
                hyp, loc, msg, data.c_str(), (long long)ms);
        fclose(f);
    }
}
// #endregion

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
    std::chrono::steady_clock::time_point last_record_stop_time_;
    bool has_recent_record_stop_;

    static const char* cardStateToString(ins_camera::CardState state) {
        switch (state) {
            case ins_camera::STOR_CS_PASS:
                return "PASS";
            case ins_camera::STOR_CS_NOCARD:
                return "NO_CARD";
            case ins_camera::STOR_CS_NOSPACE:
                return "NO_SPACE";
            case ins_camera::STOR_CS_INVALID_FORMAT:
                return "INVALID_FORMAT";
            case ins_camera::STOR_CS_WPCARD:
                return "WRITE_PROTECTED";
            case ins_camera::STOR_CS_OTHER_ERROR:
                return "OTHER_ERROR";
            default:
                return "UNKNOWN";
        }
    }

    static std::string formatBytes(uint64_t bytes) {
        constexpr uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
        constexpr uint64_t kMiB = 1024ULL * 1024ULL;
        if (bytes >= kGiB) {
            return std::to_string(bytes / kGiB) + " GiB";
        }
        return std::to_string(bytes / kMiB) + " MiB";
    }

    bool preflightStorageForPhoto() {
        ins_camera::StorageStatus status{};
        if (!camera_->GetStorageState(status)) {
            std::cerr << "Error: Failed to read camera storage state before photo capture." << std::endl;
            return false;
        }

        std::cout << "Storage state before photo: " << cardStateToString(status.state)
                  << " (free=" << formatBytes(status.free_space)
                  << ", total=" << formatBytes(status.total_space) << ")" << std::endl;

        if (status.state != ins_camera::STOR_CS_PASS) {
            std::cerr << "Error: Storage is not ready for photo capture (state="
                      << cardStateToString(status.state) << ")." << std::endl;
            return false;
        }

        constexpr uint64_t kMinimumFreeBytes = 50ULL * 1024ULL * 1024ULL; // 50 MiB safety floor
        if (status.free_space < kMinimumFreeBytes) {
            std::cerr << "Error: Insufficient free storage for reliable photo capture. "
                      << "Need at least " << formatBytes(kMinimumFreeBytes) << "." << std::endl;
            return false;
        }

        return true;
    }

public:
    CameraController() : is_connected_(false), has_recent_record_stop_(false) {}

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
        // #region agent log
        debugLog("H5", "discoverAndConnect:connected", "Camera connected, checking initial state",
                 "isConnected=" + std::to_string(camera_->IsConnected()));
        bool initBusy = camera_->CaptureCurrentStatus();
        debugLog("H5", "discoverAndConnect:initialStatus", "Initial CaptureCurrentStatus",
                 "busy=" + std::to_string(initBusy));
        // #endregion
        
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

        // #region agent log
        debugLog("H6", "takePhoto:SetVideoSubMode_before", "Resetting video pipeline before photo mode switch");
        // #endregion
        std::cout << "Resetting camera mode via video sub-mode..." << std::endl;
        bool video_reset = camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_NORMAL);
        // #region agent log
        debugLog("H6", "takePhoto:SetVideoSubMode_after", "SetVideoSubMode returned", "result=" + std::to_string(video_reset));
        // #endregion
        if (!video_reset) {
            std::cerr << "Warning: Video mode reset failed, proceeding anyway." << std::endl;
        }

        constexpr int kPhotoModeAttempts = 3;
        bool mode_set = false;
        for (int attempt = 1; attempt <= kPhotoModeAttempts; ++attempt) {
            std::cout << "Setting photo mode (attempt " << attempt << "/" << kPhotoModeAttempts << ")..." << std::endl;
            // #region agent log
            debugLog("H6", "takePhoto:SetPhotoSubMode_before", "About to call SetPhotoSubMode", "attempt=" + std::to_string(attempt));
            // #endregion
            mode_set = camera_->SetPhotoSubMode(ins_camera::SubPhotoMode::PHOTO_SINGLE);
            // #region agent log
            debugLog("H6", "takePhoto:SetPhotoSubMode_after", "SetPhotoSubMode returned", "result=" + std::to_string(mode_set));
            // #endregion
            if (mode_set) {
                break;
            }
            std::cerr << "Warning: Failed to set photo mode on attempt " << attempt << "." << std::endl;
            if (attempt < kPhotoModeAttempts) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        if (!mode_set) {
            std::cerr << "Error: Unable to switch camera to single photo mode." << std::endl;
            return false;
        }

        if (!preflightStorageForPhoto()) {
            return false;
        }

        constexpr int kTakePhotoAttempts = 3;
        std::string photo_url;
        bool photo_captured = false;
        for (int attempt = 1; attempt <= kTakePhotoAttempts; ++attempt) {
            std::cout << "Taking photo (attempt " << attempt << "/" << kTakePhotoAttempts
                      << ")..." << std::endl;
            // #region agent log
            debugLog("H7", "takePhoto:TakePhoto_before", "About to call TakePhoto()", "attempt=" + std::to_string(attempt));
            // #endregion
            const auto url = camera_->TakePhoto();
            // #region agent log
            debugLog("H7", "takePhoto:TakePhoto_after", "TakePhoto() returned", "empty=" + std::to_string(url.Empty()) + ",singleOrigin=" + std::to_string(url.IsSingleOrigin()));
            // #endregion
            std::cout << "Capture command returned from SDK." << std::endl;

            if (!url.Empty() && url.IsSingleOrigin()) {
                photo_url = url.GetSingleOrigin();
                photo_captured = true;
                break;
            }

            std::cerr << "Warning: Photo attempt " << attempt
                      << " failed (empty or invalid media URL)." << std::endl;
            if (attempt < kTakePhotoAttempts) {
                std::cout << "Re-applying photo mode before retry..." << std::endl;
                // #region agent log
                debugLog("H7", "takePhoto:retry_before", "Re-applying modes before retry", "attempt=" + std::to_string(attempt));
                // #endregion
                camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_NORMAL);
                bool reapply = camera_->SetPhotoSubMode(ins_camera::SubPhotoMode::PHOTO_SINGLE);
                // #region agent log
                debugLog("H7", "takePhoto:retry_after", "Re-apply returned", "result=" + std::to_string(reapply));
                // #endregion
                if (!reapply) {
                    std::cerr << "Warning: Failed to re-apply photo mode before retry." << std::endl;
                }
                const int backoff_seconds = attempt * 2;
                std::cout << "Waiting " << backoff_seconds
                          << "s before retrying photo capture..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(backoff_seconds));
            }
        }

        if (!photo_captured) {
            std::cerr << "Error: Failed to take photo after retries." << std::endl;
            std::cerr << "Hint: If this repeats on X5, wait for camera to become idle and retry." << std::endl;
            return false;
        }

        has_recent_record_stop_ = false;
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

    bool startRecording() {
        if (!is_connected_ || !camera_) {
            std::cerr << "Error: Camera not connected." << std::endl;
            return false;
        }

        if (!camera_->IsConnected()) {
            std::cerr << "Error: Camera connection lost." << std::endl;
            is_connected_ = false;
            return false;
        }

        std::cout << "Switching camera to normal video mode..." << std::endl;
        bool mode_set = camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_NORMAL);
        if (!mode_set) {
            std::cerr << "Error: Failed to switch to normal video mode." << std::endl;
            return false;
        }

        std::cout << "Starting recording..." << std::endl;
        bool ret = camera_->StartRecording();
        if (!ret) {
            std::cerr << "Error: Failed to start recording." << std::endl;
            return false;
        }

        std::cout << "Recording started." << std::endl;
        return true;
    }

    bool stopRecording(const std::string& save_directory = "./") {
        if (!is_connected_ || !camera_) {
            std::cerr << "Error: Camera not connected." << std::endl;
            return false;
        }

        if (!camera_->IsConnected()) {
            std::cerr << "Error: Camera connection lost." << std::endl;
            is_connected_ = false;
            return false;
        }

        std::cout << "Stopping recording..." << std::endl;
        // #region agent log
        debugLog("H5", "stopRecording:StopRecording_before", "About to call StopRecording()");
        // #endregion
        auto url = camera_->StopRecording();
        // #region agent log
        debugLog("H5", "stopRecording:StopRecording_after", "StopRecording returned", "empty=" + std::to_string(url.Empty()));
        // #endregion
        if (url.Empty()) {
            std::cerr << "Error: Failed to stop recording or no recording in progress." << std::endl;
            return false;
        }

        if (!url.IsSingleOrigin()) {
            std::cerr << "Error: Recording stopped, but returned URL is not a single origin file." << std::endl;
            return false;
        }

        const std::string video_url = url.GetSingleOrigin();
        std::cout << "Recording stopped. Video URL: " << video_url << std::endl;
        last_record_stop_time_ = std::chrono::steady_clock::now();
        has_recent_record_stop_ = true;
        std::cout << "Recorded stop timestamp captured; next photo will apply post-record readiness checks." << std::endl;

        if (!save_directory.empty()) {
            std::string save_path = save_directory;
            if (save_path.back() != '/' && save_path.back() != '\\') {
                save_path += "/";
            }

            if (!fileExists(save_path)) {
                std::cerr << "Error: Save directory does not exist: " << save_path << std::endl;
                std::cerr << "Video URL on camera: " << video_url << std::endl;
                return false;
            }

            std::string file_name = getFileName(video_url);
            if (file_name.empty()) {
                file_name = "video_" + getCurrentTime() + ".mp4";
            }

            std::string full_path = save_path + file_name;
            std::cout << "Downloading video to: " << full_path << std::endl;

            int64_t last_progress = -1;
            bool download_success = camera_->DownloadCameraFile(video_url, full_path,
                [&](int64_t current, int64_t total_size) {
                    int64_t progress = total_size > 0 ? (current * 100 / total_size) : 0;
                    if (progress != last_progress) {
                        std::cout << "\rDownload progress: " << progress << "%" << std::flush;
                        last_progress = progress;
                    }
                });

            std::cout << std::endl;

            if (download_success) {
                std::cout << "Video successfully downloaded to: " << full_path << std::endl;
                return true;
            } else {
                std::cerr << "Error: Failed to download video." << std::endl;
                std::cerr << "Video URL on camera: " << video_url << std::endl;
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
    std::cout << "       " << program_name << " record <start|stop>" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  connect              - Connect to camera" << std::endl;
    std::cout << "  photo [save_dir]    - Take a photo (optionally save to directory)" << std::endl;
    std::cout << "  record start        - Start video recording" << std::endl;
    std::cout << "  record stop [dir]   - Stop video recording and download to directory" << std::endl;
    std::cout << "  record-start        - Start video recording (alias)" << std::endl;
    std::cout << "  record-stop [dir]   - Stop video recording and download (alias)" << std::endl;
    std::cout << "  shutdown             - Power off the camera" << std::endl;
    std::cout << "  battery              - Get battery status" << std::endl;
    std::cout << "  interactive          - Interactive mode" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " photo                    # Take photo" << std::endl;
    std::cout << "  " << program_name << " photo ./photos          # Take photo and save to ./photos" << std::endl;
    std::cout << "  " << program_name << " record-start            # Start recording (alias)" << std::endl;
    std::cout << "  " << program_name << " record-stop ./videos    # Stop recording and save (alias)" << std::endl;
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
        std::cout << "Camera connected. Use 'photo', 'record', 'shutdown', or 'battery' commands." << std::endl;
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
    else if (command == "record-start") {
        bool success = controller.startRecording();
        controller.disconnect();
        return success ? 0 : 1;
    }
    else if (command == "record-stop") {
        std::string save_dir = (argc > 2) ? argv[2] : "./";
        bool success = controller.stopRecording(save_dir);
        controller.disconnect();
        return success ? 0 : 1;
    }
    else if (command == "record") {
        if (argc < 3) {
            std::cerr << "Error: Missing record action. Use 'record start' or 'record stop'." << std::endl;
            printUsage(argv[0]);
            controller.disconnect();
            return 1;
        }

        std::string record_action = argv[2];
        if (record_action == "start") {
            bool success = controller.startRecording();
            controller.disconnect();
            return success ? 0 : 1;
        } else if (record_action == "stop") {
            std::string save_dir = (argc > 3) ? argv[3] : "./";
            bool success = controller.stopRecording(save_dir);
            controller.disconnect();
            return success ? 0 : 1;
        } else {
            std::cerr << "Error: Unknown record action: " << record_action << std::endl;
            std::cerr << "Use 'record start' or 'record stop'." << std::endl;
            printUsage(argv[0]);
            controller.disconnect();
            return 1;
        }
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
        std::cout << "Commands: photo [dir], record start, record stop [dir], shutdown, battery, quit" << std::endl;
        
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
            else if (line == "record start") {
                controller.startRecording();
            }
            else if (line == "record stop") {
                controller.stopRecording("./");
            }
            else if (line.substr(0, 12) == "record stop ") {
                std::string dir = line.length() > 12 ? line.substr(12) : "./";
                controller.stopRecording(dir);
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
                std::cout << "Unknown command. Try: photo, record start, record stop [dir], shutdown, battery, quit" << std::endl;
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

