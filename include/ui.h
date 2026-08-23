#pragma once

#include "youtube_searcher.h"
#include "audio_player.h"
#include "playlist_manager.h"

#include <ncurses.h>
#include <string>
#include <vector>
#include <memory>

/// Input mode determines where keyboard events are routed.
enum class InputMode {
    NORMAL,       ///< Arrow keys navigate, shortcut keys active
    SEARCH,       ///< Typing into the search box
    SAVE_PROMPT,  ///< Typing a playlist name to save
    LOAD_LIST,    ///< Showing saved playlists, arrow keys to select
};

/// Which scrollable panel has focus for arrow key navigation.
enum class FocusPanel {
    RESULTS,
    PLAYLIST,
};

/// The full-screen ncurses terminal UI.
///
/// Manages four panels: Now Playing, Search/Results, Playlist, and a
/// status bar with keyboard shortcut hints. Drives the main event loop.
class TerminalUI {
public:
    TerminalUI(YouTubeSearcher& searcher,
               AudioPlayer& player,
               PlaylistManager& playlist);
    ~TerminalUI();

    /// Initialize ncurses, create windows, set up colors.
    void init();

    /// Main event loop. Blocks until the user quits (Q).
    void run();

    /// Clean up ncurses state and close windows.
    void cleanup();

private:
    // ── Input handling ───────────────────────────────────────────────
    void handleInput(int ch);
    void handleNormalInput(int ch);
    void handleSearchInput(int ch);
    void handleSavePromptInput(int ch);
    void handleLoadListInput(int ch);

    // ── Drawing ──────────────────────────────────────────────────────
    void drawAll();
    void drawNowPlaying();
    void drawSearchBox();
    void drawResults();
    void drawPlaylist();
    void drawStatusBar();
    void drawLoadList();          ///< Overlay for playlist loading
    void drawErrorMessage();      ///< Flash error if one is pending
    void drawProgressBar(WINDOW* win, int y, int x, int width,
                         double current, double total);

    // ── Window management ────────────────────────────────────────────
    void createWindows();
    void destroyWindows();
    void recalculateLayout();

    // ── Actions ──────────────────────────────────────────────────────
    void doSearch();
    void playSelectedResult();
    void playSelectedPlaylistSong();
    void addSelectedToPlaylist();
    void removeSelectedFromPlaylist();
    void doSavePlaylist();
    void doLoadPlaylist();
    void playNextSong();
    void playPrevSong();

    // ── Helper ───────────────────────────────────────────────────────
    /// Truncate a string to fit in maxLen columns, appending "…" if needed.
    std::string truncate(const std::string& str, size_t maxLen) const;

    /// Show a temporary status/error message for a few seconds.
    void setStatusMessage(const std::string& msg, bool isError = false);

    // ── Component references ─────────────────────────────────────────
    YouTubeSearcher& searcher_;
    AudioPlayer& player_;
    PlaylistManager& playlist_;

    // ── UI state ─────────────────────────────────────────────────────
    InputMode mode_;
    FocusPanel focusPanel_;

    std::string searchInput_;           ///< Current search query being typed
    std::vector<Song> searchResults_;   ///< Latest search results
    int selectedResult_;                ///< Index in searchResults_
    int selectedPlaylist_;              ///< Index in playlist songs
    int resultScrollOffset_;            ///< Scroll offset for results list
    int playlistScrollOffset_;          ///< Scroll offset for playlist list

    std::string saveInput_;             ///< Playlist name being typed for save
    std::vector<std::string> savedPlaylists_;  ///< List for load overlay
    int selectedSavedPlaylist_;         ///< Index in savedPlaylists_

    std::string statusMessage_;         ///< Temporary status/error message
    bool statusIsError_;                ///< Is the status message an error?
    int statusTimer_;                   ///< Frames remaining to display status

    bool running_;                      ///< Main loop flag
    bool searching_;                    ///< True while a search is in progress

    // ── ncurses windows ──────────────────────────────────────────────
    WINDOW* winNowPlaying_;
    WINDOW* winSearch_;
    WINDOW* winResults_;
    WINDOW* winPlaylist_;
    WINDOW* winStatus_;

    // ── Layout dimensions ────────────────────────────────────────────
    int termRows_, termCols_;           ///< Current terminal size
};
