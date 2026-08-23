#include "audio_player.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>

using json = nlohmann::json;

AudioPlayer::AudioPlayer()
    : ipcSocketPath_("/tmp/mpv-music-player-" + std::to_string(getpid()))
    , mpvPid_(-1)
    , ipcFd_(-1)
    , playing_(false)
    , paused_(false)
    , volume_(80)
{
}

AudioPlayer::~AudioPlayer() {
    stop();
    // Clean up socket file
    unlink(ipcSocketPath_.c_str());
}

bool AudioPlayer::play(const Song& song, const std::string& streamURL) {
    // Stop any current playback
    stop();

    if (streamURL.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = "Empty stream URL";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentSong_ = song;
    }

    // Remove any stale socket file
    unlink(ipcSocketPath_.c_str());

    // Fork and exec mpv
    pid_t pid = fork();
    if (pid < 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = "Failed to fork mpv process";
        return false;
    }

    if (pid == 0) {
        // Child process: exec mpv
        // Redirect stdout and stderr to /dev/null
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        std::string volStr = std::to_string(volume_.load());
        std::string ipcArg = "--input-ipc-server=" + ipcSocketPath_;

        execlp("mpv", "mpv",
               "--no-video",
               "--really-quiet",
               "--no-terminal",
               ipcArg.c_str(),
               ("--volume=" + volStr).c_str(),
               streamURL.c_str(),
               nullptr);

        // If exec fails, exit the child
        _exit(127);
    }

    // Parent process
    mpvPid_ = pid;
    playing_ = true;
    paused_ = false;

    // Wait for mpv to create the IPC socket (up to 3 seconds)
    bool connected = false;
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (connectIPC()) {
            connected = true;
            break;
        }
    }

    if (!connected) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = "Failed to connect to mpv IPC socket";
        // mpv might still be running, keep it alive
    }

    return true;
}

void AudioPlayer::togglePause() {
    if (!playing_) return;

    // Send cycle pause command
    json cmd;
    cmd["command"] = json::array({"cycle", "pause"});
    sendIPCCommand(cmd.dump());

    paused_ = !paused_;
}

void AudioPlayer::pause() {
    if (!playing_ || paused_) return;

    json cmd;
    cmd["command"] = json::array({"set_property", "pause", true});
    sendIPCCommand(cmd.dump());

    paused_ = true;
}

void AudioPlayer::resume() {
    if (!playing_ || !paused_) return;

    json cmd;
    cmd["command"] = json::array({"set_property", "pause", false});
    sendIPCCommand(cmd.dump());

    paused_ = false;
}

void AudioPlayer::stop() {
    if (ipcFd_ >= 0) {
        // Try to send quit command gracefully
        json cmd;
        cmd["command"] = json::array({"quit"});
        sendIPCCommand(cmd.dump());
    }

    disconnectIPC();
    killMpv();

    playing_ = false;
    paused_ = false;

    // Clean up socket file
    unlink(ipcSocketPath_.c_str());
}

void AudioPlayer::setVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    volume_ = vol;

    if (playing_ && ipcFd_ >= 0) {
        json cmd;
        cmd["command"] = json::array({"set_property", "volume", vol});
        sendIPCCommand(cmd.dump());
    }
}

int AudioPlayer::getVolume() const {
    return volume_.load();
}

double AudioPlayer::getPosition() {
    std::string val = getProperty("time-pos");
    if (val.empty()) return 0.0;
    try {
        return std::stod(val);
    } catch (...) {
        return 0.0;
    }
}

double AudioPlayer::getDuration() {
    std::string val = getProperty("duration");
    if (val.empty()) return 0.0;
    try {
        return std::stod(val);
    } catch (...) {
        return 0.0;
    }
}

bool AudioPlayer::isPlaying() const {
    return playing_ && !paused_;
}

bool AudioPlayer::isPaused() const {
    return playing_ && paused_;
}

bool AudioPlayer::hasFinished() {
    if (!playing_ || mpvPid_ <= 0) return true;

    // Check if mpv process is still alive
    int status;
    pid_t result = waitpid(mpvPid_, &status, WNOHANG);
    if (result == mpvPid_) {
        // Process has exited
        playing_ = false;
        paused_ = false;
        mpvPid_ = -1;
        disconnectIPC();
        return true;
    }
    return false;
}

Song AudioPlayer::getCurrentSong() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentSong_;
}

std::string AudioPlayer::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

// ── Private methods ──────────────────────────────────────────────────

std::string AudioPlayer::sendIPCCommand(const std::string& jsonCmd) {
    if (ipcFd_ < 0) {
        // Try to reconnect
        if (!connectIPC()) return "";
    }

    // mpv IPC expects newline-terminated JSON
    std::string msg = jsonCmd + "\n";
    ssize_t sent = write(ipcFd_, msg.c_str(), msg.size());
    if (sent < 0) {
        disconnectIPC();
        return "";
    }

    // Read response (with timeout)
    struct pollfd pfd;
    pfd.fd = ipcFd_;
    pfd.events = POLLIN;

    if (poll(&pfd, 1, 500) <= 0) {  // 500ms timeout
        return "";
    }

    char buffer[4096] = {};
    ssize_t bytesRead = read(ipcFd_, buffer, sizeof(buffer) - 1);
    if (bytesRead <= 0) {
        disconnectIPC();
        return "";
    }

    return std::string(buffer, bytesRead);
}

std::string AudioPlayer::getProperty(const std::string& property) {
    if (ipcFd_ < 0 && !connectIPC()) return "";

    json cmd;
    cmd["command"] = json::array({"get_property", property});
    std::string response = sendIPCCommand(cmd.dump());

    if (response.empty()) return "";

    // Response may contain multiple lines; parse the relevant one
    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        line = Utils::trim(line);
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            if (j.contains("data") && !j["data"].is_null()) {
                if (j["data"].is_number()) {
                    return std::to_string(j["data"].get<double>());
                }
                if (j["data"].is_string()) {
                    return j["data"].get<std::string>();
                }
                if (j["data"].is_boolean()) {
                    return j["data"].get<bool>() ? "true" : "false";
                }
            }
        } catch (...) {
            continue;
        }
    }
    return "";
}

bool AudioPlayer::connectIPC() {
    if (ipcFd_ >= 0) return true;  // Already connected

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, ipcSocketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    // Set non-blocking for reads
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ipcFd_ = fd;
    return true;
}

void AudioPlayer::disconnectIPC() {
    if (ipcFd_ >= 0) {
        close(ipcFd_);
        ipcFd_ = -1;
    }
}

void AudioPlayer::killMpv() {
    if (mpvPid_ > 0) {
        // Send SIGTERM first, then SIGKILL if needed
        kill(mpvPid_, SIGTERM);

        // Wait up to 1 second for graceful exit
        for (int i = 0; i < 10; ++i) {
            int status;
            pid_t result = waitpid(mpvPid_, &status, WNOHANG);
            if (result == mpvPid_) {
                mpvPid_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Force kill
        kill(mpvPid_, SIGKILL);
        waitpid(mpvPid_, nullptr, 0);
        mpvPid_ = -1;
    }
}
