#pragma once

#include <string>
#include <utility>

namespace Utils {

/// Execute a shell command and capture its stdout.
/// Returns a pair of (exit_code, stdout_output).
std::pair<int, std::string> exec(const std::string& cmd);

/// Get the user's home directory path.
std::string getHomeDirectory();

/// Get the playlist storage directory (~/.music_playlists/), creating it if needed.
std::string getPlaylistDir();

/// Remove leading and trailing whitespace from a string.
std::string trim(const std::string& str);

/// URL-encode a string for use in yt-dlp queries.
std::string urlEncode(const std::string& str);

/// Format seconds as MM:SS.
std::string formatTime(int totalSeconds);

/// Check whether a file exists at the given path.
bool fileExists(const std::string& path);

/// Create a directory (and parents) if it doesn't exist.
bool createDirectory(const std::string& path);

} // namespace Utils
