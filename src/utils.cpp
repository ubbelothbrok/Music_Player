#include "utils.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

namespace Utils {

std::pair<int, std::string> exec(const std::string& cmd) {
    // Redirect stderr to stdout so we capture everything
    std::string fullCmd = cmd + " 2>&1";

    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        return {-1, "Failed to execute command: " + cmd};
    }

    std::string result;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

    int status = pclose(pipe);
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {exitCode, result};
}

std::string getHomeDirectory() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home);
    }
    // Fallback to passwd entry
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return std::string(pw->pw_dir);
    }
    return "/tmp"; // Last resort
}

std::string getPlaylistDir() {
    std::string dir = getHomeDirectory() + "/.music_playlists";
    createDirectory(dir);
    return dir;
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

std::string urlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (unsigned char c : str) {
        // Keep alphanumeric and some safe characters
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ' ') {
            if (c == ' ') {
                encoded << '+';
            } else {
                encoded << c;
            }
        } else {
            encoded << '%' << std::setw(2) << std::uppercase << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string formatTime(int totalSeconds) {
    if (totalSeconds < 0) totalSeconds = 0;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << minutes
        << ":" << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}

bool fileExists(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0);
}

bool createDirectory(const std::string& path) {
    if (fileExists(path)) return true;

    // Try creating the directory with rwxr-xr-x permissions
    int result = mkdir(path.c_str(), 0755);
    if (result == 0) return true;

    // If parent doesn't exist, create it recursively
    if (errno == ENOENT) {
        size_t pos = path.find_last_of('/');
        if (pos != std::string::npos && pos > 0) {
            if (!createDirectory(path.substr(0, pos))) {
                return false;
            }
            return mkdir(path.c_str(), 0755) == 0;
        }
    }
    return false;
}

} // namespace Utils
