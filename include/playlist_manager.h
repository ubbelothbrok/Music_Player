#pragma once

#include "youtube_searcher.h"

#include <string>
#include <vector>

/// Manages the current in-memory playlist and persistent storage
/// of playlists as JSON files in ~/.music_playlists/.
class PlaylistManager {
public:
    PlaylistManager();

    /// Add a song to the end of the current playlist.
    void addSong(const Song& song);

    /// Remove the song at the given index. Returns false if out of range.
    bool removeSong(size_t index);

    /// Save the current playlist to a JSON file with the given name.
    /// Returns true on success.
    bool savePlaylist(const std::string& name);

    /// Load a playlist from a JSON file. Returns true on success.
    bool loadPlaylist(const std::string& name);

    /// List all saved playlist names (scans ~/.music_playlists/ for .json files).
    std::vector<std::string> listPlaylists() const;

    /// Get a const reference to all songs in the current playlist.
    const std::vector<Song>& getSongs() const;

    /// Get song at the given index. Returns a default Song if out of range.
    Song getSongAt(size_t index) const;

    /// Number of songs in the current playlist.
    size_t size() const;

    /// Clear all songs from the current playlist.
    void clear();

    /// Get the index of the currently active song (for next/prev navigation).
    int getCurrentIndex() const;

    /// Set the currently active song index.
    void setCurrentIndex(int index);

    /// Advance to the next song. Wraps around. Returns the next Song.
    Song nextSong();

    /// Go to the previous song. Wraps around. Returns the previous Song.
    Song prevSong();

    /// Get the name of the current playlist.
    std::string getPlaylistName() const;

    /// Set the name of the current playlist.
    void setPlaylistName(const std::string& name);

    /// Shuffle the playlist in random order.
    void shuffle();

    /// Get last error message.
    std::string getLastError() const;

private:
    std::vector<Song> songs_;       
    std::string playlistName_;     
    int currentIndex_;             
    std::string lastError_;        
};
