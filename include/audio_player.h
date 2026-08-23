#pragma once

#include "youtube_searcher.h"

#include <atomic>
#include <mutex>
#include <string>
#include <sys/types.h>

/// Audio playback engine using mpv with IPC socket control.
///
/// Spawns mpv as a child process in --no-video mode and communicates
/// via a Unix domain socket (--input-ipc-server) to control playback
/// and query position/duration.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    /// Start playing the given song. Extracts stream URL and spawns mpv.
    /// If something is already playing, it is stopped first.
    /// Returns true on success.
    bool play(const Song& song, const std::string& streamURL);

    /// Toggle pause/resume.
    void togglePause();

    /// Pause playback.
    void pause();

    /// Resume playback.
    void resume();

    /// Stop playback and kill the mpv process.
    void stop();

    /// Set volume (0–100).
    void setVolume(int volume);

    /// Get current volume (0–100).
    int getVolume() const;

    /// Get current playback position in seconds.
    double getPosition();

    /// Get total duration in seconds.
    double getDuration();

    /// True if mpv is actively playing audio (not paused, not stopped).
    bool isPlaying() const;

    /// True if playback is paused.
    bool isPaused() const;

    /// True if playback has finished (mpv process exited).
    bool hasFinished();

    /// Get the currently playing song metadata.
    Song getCurrentSong() const;

    /// Get last error message.
    std::string getLastError() const;

private:
    /// Send a JSON command to mpv via IPC socket.
    /// Returns the response string, or empty on failure.
    std::string sendIPCCommand(const std::string& jsonCmd);

    /// Query a property from mpv. Returns the JSON value string.
    std::string getProperty(const std::string& property);

    /// Connect to the mpv IPC socket. Returns true on success.
    bool connectIPC();

    /// Disconnect from the IPC socket.
    void disconnectIPC();

    /// Kill the current mpv child process.
    void killMpv();

    /// Path to the IPC socket file.
    std::string ipcSocketPath_;

    /// mpv child process ID.
    pid_t mpvPid_;

    /// IPC socket file descriptor.
    int ipcFd_;

    /// Currently playing song.
    Song currentSong_;

    /// Playback state flags.
    std::atomic<bool> playing_;
    std::atomic<bool> paused_;

    /// Current volume level (0–100).
    std::atomic<int> volume_;

    /// Last error message.
    std::string lastError_;

    /// Mutex for thread safety.
    mutable std::mutex mutex_;
};
