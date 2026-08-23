#include "playlist_manager.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

using json = nlohmann::json;

PlaylistManager::PlaylistManager()
    : playlistName_("Untitled Playlist")
    , currentIndex_(-1)
{
}

void PlaylistManager::addSong(const Song& song) {
    songs_.push_back(song);
    // If this is the first song, set current index to 0
    if (songs_.size() == 1) {
        currentIndex_ = 0;
    }
}

bool PlaylistManager::removeSong(size_t index) {
    if (index >= songs_.size()) {
        lastError_ = "Index out of range";
        return false;
    }

    songs_.erase(songs_.begin() + static_cast<long>(index));

    // Adjust current index
    if (songs_.empty()) {
        currentIndex_ = -1;
    } else if (currentIndex_ >= static_cast<int>(songs_.size())) {
        currentIndex_ = static_cast<int>(songs_.size()) - 1;
    }

    return true;
}

bool PlaylistManager::savePlaylist(const std::string& name) {
    std::string dir = Utils::getPlaylistDir();
    std::string filePath = dir + "/" + name + ".json";

    // Build timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeStream;
    timeStream << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");

    // Serialize to JSON
    json j;
    j["name"] = name;
    j["created"] = timeStream.str();
    j["songs"] = json::array();

    for (const auto& song : songs_) {
        json songJson;
        songJson["id"]       = song.id;
        songJson["title"]    = song.title;
        songJson["artist"]   = song.artist;
        songJson["duration"] = song.duration;
        songJson["url"]      = song.url;
        j["songs"].push_back(songJson);
    }

    // Write to file
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        lastError_ = "Could not open file for writing: " + filePath;
        return false;
    }

    outFile << j.dump(2);  // Pretty-print with 2-space indent
    outFile.close();

    playlistName_ = name;
    lastError_.clear();
    return true;
}

bool PlaylistManager::loadPlaylist(const std::string& name) {
    std::string dir = Utils::getPlaylistDir();
    std::string filePath = dir + "/" + name + ".json";

    if (!Utils::fileExists(filePath)) {
        lastError_ = "Playlist file not found: " + filePath;
        return false;
    }

    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        lastError_ = "Could not open playlist file: " + filePath;
        return false;
    }

    // Read file contents
    std::string contents((std::istreambuf_iterator<char>(inFile)),
                          std::istreambuf_iterator<char>());
    inFile.close();

    // Parse JSON
    json j;
    try {
        j = json::parse(contents);
    } catch (const json::parse_error& e) {
        lastError_ = "Malformed JSON in playlist file: " + std::string(e.what());
        return false;
    }

    // Extract playlist data
    songs_.clear();
    currentIndex_ = -1;

    if (j.contains("name") && j["name"].is_string()) {
        playlistName_ = j["name"].get<std::string>();
    } else {
        playlistName_ = name;
    }

    if (j.contains("songs") && j["songs"].is_array()) {
        for (const auto& songJson : j["songs"]) {
            Song song;
            if (songJson.contains("id") && songJson["id"].is_string())
                song.id = songJson["id"].get<std::string>();
            if (songJson.contains("title") && songJson["title"].is_string())
                song.title = songJson["title"].get<std::string>();
            if (songJson.contains("artist") && songJson["artist"].is_string())
                song.artist = songJson["artist"].get<std::string>();
            if (songJson.contains("duration") && songJson["duration"].is_number())
                song.duration = songJson["duration"].get<int>();
            if (songJson.contains("url") && songJson["url"].is_string())
                song.url = songJson["url"].get<std::string>();
            songs_.push_back(std::move(song));
        }
    }

    if (!songs_.empty()) {
        currentIndex_ = 0;
    }

    lastError_.clear();
    return true;
}

std::vector<std::string> PlaylistManager::listPlaylists() const {
    std::vector<std::string> names;
    std::string dir = Utils::getPlaylistDir();

    DIR* dirp = opendir(dir.c_str());
    if (!dirp) return names;

    struct dirent* entry;
    while ((entry = readdir(dirp)) != nullptr) {
        std::string filename = entry->d_name;
        // Look for .json files
        if (filename.size() > 5 &&
            filename.substr(filename.size() - 5) == ".json") {
            // Remove .json extension for display
            names.push_back(filename.substr(0, filename.size() - 5));
        }
    }
    closedir(dirp);

    std::sort(names.begin(), names.end());
    return names;
}

const std::vector<Song>& PlaylistManager::getSongs() const {
    return songs_;
}

Song PlaylistManager::getSongAt(size_t index) const {
    if (index >= songs_.size()) return Song();
    return songs_[index];
}

size_t PlaylistManager::size() const {
    return songs_.size();
}

void PlaylistManager::clear() {
    songs_.clear();
    currentIndex_ = -1;
}

int PlaylistManager::getCurrentIndex() const {
    return currentIndex_;
}

void PlaylistManager::setCurrentIndex(int index) {
    if (index >= 0 && index < static_cast<int>(songs_.size())) {
        currentIndex_ = index;
    }
}

Song PlaylistManager::nextSong() {
    if (songs_.empty()) return Song();

    currentIndex_ = (currentIndex_ + 1) % static_cast<int>(songs_.size());
    return songs_[currentIndex_];
}

Song PlaylistManager::prevSong() {
    if (songs_.empty()) return Song();

    currentIndex_--;
    if (currentIndex_ < 0) {
        currentIndex_ = static_cast<int>(songs_.size()) - 1;
    }
    return songs_[currentIndex_];
}

std::string PlaylistManager::getPlaylistName() const {
    return playlistName_;
}

void PlaylistManager::setPlaylistName(const std::string& name) {
    playlistName_ = name;
}

void PlaylistManager::shuffle() {
    if (songs_.size() <= 1) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(songs_.begin(), songs_.end(), gen);

    currentIndex_ = 0;
}

std::string PlaylistManager::getLastError() const {
    return lastError_;
}
