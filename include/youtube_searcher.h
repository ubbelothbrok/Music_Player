#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// Represents a single song/video from YouTube.
struct Song {
    std::string id;        ///< YouTube video ID (e.g., "dQw4w9WgXcQ")
    std::string title;     ///< Video title
    std::string artist;    ///< Channel / uploader name
    int         duration;  ///< Duration in seconds
    std::string url;       ///< Full YouTube URL

    Song() : duration(0) {}
    Song(std::string id, std::string title, std::string artist, int duration, std::string url)
        : id(std::move(id)), title(std::move(title)), artist(std::move(artist)),
          duration(duration), url(std::move(url)) {}
};

/// Searches YouTube for songs and extracts audio stream URLs using yt-dlp.
class YouTubeSearcher {
public:
    YouTubeSearcher();

    /// Search YouTube for the given query. Returns up to `limit` results.
    /// Blocks until yt-dlp finishes (typically 2-8 seconds).
    std::vector<Song> search(const std::string& query, int limit = 10);

    /// Extract the best audio stream URL for a YouTube video ID.
    /// Returns the direct URL string, or empty string on failure.
    std::string getStreamURL(const std::string& videoId);

    /// Async version of getStreamURL. Runs in a detached thread and
    /// invokes callback(url) when done.
    void getStreamURLAsync(const std::string& videoId,
                           std::function<void(const std::string&)> callback);

    /// Get the last error message (if any operation failed).
    std::string getLastError() const;

    /// Clear the search cache.
    void clearCache();

private:
    /// Parse a single JSON object (one line of yt-dlp --dump-json output) into a Song.
    Song parseSongFromJSON(const std::string& jsonStr);

    /// Search result cache: query → results
    std::unordered_map<std::string, std::vector<Song>> cache_;

    /// Last error message
    std::string lastError_;

    /// Mutex for thread safety
    mutable std::mutex mutex_;
};
