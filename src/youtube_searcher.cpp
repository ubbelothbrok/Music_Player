#include "youtube_searcher.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

using json = nlohmann::json;

YouTubeSearcher::YouTubeSearcher() {}

std::vector<Song> YouTubeSearcher::search(const std::string& query, int limit) {
    if (query.empty()) return {};

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(query);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    // Build yt-dlp command for searching YouTube
    // --flat-playlist is fast: fetches metadata without extracting streams
    std::string cmd = "yt-dlp \"ytsearch" + std::to_string(limit) + ":" + query +
                      "\" --dump-json --flat-playlist --no-download --no-warnings "
                      "--no-check-certificates --prefer-free-formats "
                      "--socket-timeout 15 2>/dev/null";

    auto [exitCode, output] = Utils::exec(cmd);

    std::vector<Song> results;

    if (exitCode != 0 || output.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = "Search failed. Is yt-dlp installed and network available?";
        return results;
    }

    // yt-dlp outputs one JSON object per line
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Utils::trim(line);
        if (line.empty() || line[0] != '{') continue;

        try {
            Song song = parseSongFromJSON(line);
            if (!song.id.empty()) {
                results.push_back(std::move(song));
            }
        } catch (const std::exception& e) {
            // Skip malformed entries silently
            continue;
        }
    }

    // Cache the results
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[query] = results;
        lastError_.clear();
    }

    return results;
}

std::string YouTubeSearcher::getStreamURL(const std::string& videoId) {
    if (videoId.empty()) return "";

    // Extract the best audio-only stream URL
    std::string cmd = "yt-dlp -f bestaudio --no-warnings --no-check-certificates "
                      "--socket-timeout 15 -g "
                      "\"https://www.youtube.com/watch?v=" + videoId + "\" 2>/dev/null";

    auto [exitCode, output] = Utils::exec(cmd);

    if (exitCode != 0 || output.empty()) {
        // Fallback: try best format (not audio-only)
        cmd = "yt-dlp -f best --no-warnings --no-check-certificates "
              "--socket-timeout 15 -g "
              "\"https://www.youtube.com/watch?v=" + videoId + "\" 2>/dev/null";

        auto [exitCode2, output2] = Utils::exec(cmd);
        if (exitCode2 != 0 || output2.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            lastError_ = "Failed to extract stream URL for video: " + videoId;
            return "";
        }
        output = output2;
    }

    std::string url = Utils::trim(output);

    // If multiple URLs (video+audio), take the first one
    size_t newline = url.find('\n');
    if (newline != std::string::npos) {
        url = url.substr(0, newline);
    }

    return url;
}

void YouTubeSearcher::getStreamURLAsync(const std::string& videoId,
                                         std::function<void(const std::string&)> callback) {
    std::thread([this, videoId, callback]() {
        std::string url = getStreamURL(videoId);
        if (callback) {
            callback(url);
        }
    }).detach();
}

std::string YouTubeSearcher::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void YouTubeSearcher::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

Song YouTubeSearcher::parseSongFromJSON(const std::string& jsonStr) {
    json j = json::parse(jsonStr);

    Song song;

    // Video ID
    if (j.contains("id") && j["id"].is_string()) {
        song.id = j["id"].get<std::string>();
    }

    // Title
    if (j.contains("title") && j["title"].is_string()) {
        song.title = j["title"].get<std::string>();
    } else if (j.contains("fulltitle") && j["fulltitle"].is_string()) {
        song.title = j["fulltitle"].get<std::string>();
    }

    // Artist / channel name
    if (j.contains("uploader") && j["uploader"].is_string()) {
        song.artist = j["uploader"].get<std::string>();
    } else if (j.contains("channel") && j["channel"].is_string()) {
        song.artist = j["channel"].get<std::string>();
    } else if (j.contains("artist") && j["artist"].is_string()) {
        song.artist = j["artist"].get<std::string>();
    }

    // Duration in seconds
    if (j.contains("duration") && j["duration"].is_number()) {
        song.duration = j["duration"].get<int>();
    }

    // Build YouTube URL
    if (!song.id.empty()) {
        song.url = "https://www.youtube.com/watch?v=" + song.id;
    } else if (j.contains("url") && j["url"].is_string()) {
        song.url = j["url"].get<std::string>();
    } else if (j.contains("webpage_url") && j["webpage_url"].is_string()) {
        song.url = j["webpage_url"].get<std::string>();
    }

    return song;
}
