#include "ui.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <locale.h>
#include <thread>

// ── Color pair IDs ───────────────────────────────────────────────────
enum ColorPairs {
    CP_TITLE     = 1,  // Cyan   — section titles / headers
    CP_PLAYING   = 2,  // Green  — now-playing song info
    CP_SELECTED  = 3,  // Black on Cyan — highlighted list item
    CP_BAR_FILL  = 4,  // Black on Green — progress bar filled portion
    CP_BAR_EMPTY = 5,  // White  — progress bar empty portion
    CP_ERROR     = 6,  // Red    — error messages
    CP_HINT      = 7,  // Yellow — shortcut hints in status bar
    CP_BORDER    = 8,  // Blue   — window borders
    CP_DIM       = 9,  // Dim white — secondary info
    CP_ACCENT    = 10, // Magenta — accents
};

// ── Layout constants ─────────────────────────────────────────────────
static constexpr int NOW_PLAYING_HEIGHT = 5;
static constexpr int STATUS_BAR_HEIGHT  = 3;
static constexpr int MIN_COLS           = 60;
static constexpr int MIN_ROWS           = 18;

// ─────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────

TerminalUI::TerminalUI(YouTubeSearcher& searcher,
                       AudioPlayer& player,
                       PlaylistManager& playlist)
    : searcher_(searcher)
    , player_(player)
    , playlist_(playlist)
    , mode_(InputMode::NORMAL)
    , focusPanel_(FocusPanel::RESULTS)
    , selectedResult_(0)
    , selectedPlaylist_(0)
    , resultScrollOffset_(0)
    , playlistScrollOffset_(0)
    , selectedSavedPlaylist_(0)
    , statusIsError_(false)
    , statusTimer_(0)
    , running_(false)
    , searching_(false)
    , winNowPlaying_(nullptr)
    , winSearch_(nullptr)
    , winResults_(nullptr)
    , winPlaylist_(nullptr)
    , winStatus_(nullptr)
    , termRows_(0)
    , termCols_(0)
{
}

TerminalUI::~TerminalUI() {
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────

void TerminalUI::init() {
    // Enable UTF-8 locale
    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // Hide cursor
    nodelay(stdscr, TRUE);  // Non-blocking getch
    mousemask(0, nullptr);  // Disable mouse

    // Initialize colors
    if (has_colors()) {
        start_color();
        use_default_colors();

        init_pair(CP_TITLE,     COLOR_CYAN,    -1);
        init_pair(CP_PLAYING,   COLOR_GREEN,   -1);
        init_pair(CP_SELECTED,  COLOR_BLACK,   COLOR_CYAN);
        init_pair(CP_BAR_FILL,  COLOR_CYAN,    -1);
        init_pair(CP_BAR_EMPTY, COLOR_WHITE,   -1);
        init_pair(CP_ERROR,     COLOR_RED,     -1);
        init_pair(CP_HINT,      COLOR_YELLOW,  -1);
        init_pair(CP_BORDER,    COLOR_BLUE,    -1);
        init_pair(CP_DIM,       COLOR_WHITE,   -1);
        init_pair(CP_ACCENT,    COLOR_MAGENTA, -1);
    }

    getmaxyx(stdscr, termRows_, termCols_);
    createWindows();
}

// ─────────────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────────────

void TerminalUI::run() {
    running_ = true;

    while (running_) {
        // Handle input (non-blocking)
        int ch = getch();
        while (ch != ERR) {
            if (ch == KEY_RESIZE) {
                getmaxyx(stdscr, termRows_, termCols_);
                destroyWindows();
                createWindows();
            } else {
                handleInput(ch);
            }
            ch = getch();
        }

        // Check if playback finished → auto-advance
        if (player_.isPlaying() || player_.isPaused()) {
            if (player_.hasFinished()) {
                // Auto-play next song in playlist
                if (playlist_.size() > 0 &&
                    playlist_.getCurrentIndex() < static_cast<int>(playlist_.size()) - 1) {
                    playNextSong();
                }
            }
        }

        // Decrement status message timer
        if (statusTimer_ > 0) {
            statusTimer_--;
            if (statusTimer_ == 0) {
                statusMessage_.clear();
            }
        }

        // Redraw
        drawAll();

        // Sleep ~50ms (20 FPS) to keep CPU usage low
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void TerminalUI::cleanup() {
    destroyWindows();
    endwin();
}

// ─────────────────────────────────────────────────────────────────────
// Input Dispatch
// ─────────────────────────────────────────────────────────────────────

void TerminalUI::handleInput(int ch) {
    switch (mode_) {
        case InputMode::NORMAL:      handleNormalInput(ch);    break;
        case InputMode::SEARCH:      handleSearchInput(ch);    break;
        case InputMode::SAVE_PROMPT: handleSavePromptInput(ch); break;
        case InputMode::LOAD_LIST:   handleLoadListInput(ch);  break;
    }
}

void TerminalUI::handleNormalInput(int ch) {
    switch (ch) {
        // ── Quit ─────────────────────────────────────────────────
        case 'q':
        case 'Q':
            player_.stop();
            running_ = false;
            break;

        // ── Search mode ──────────────────────────────────────────
        case '/':
            mode_ = InputMode::SEARCH;
            searchInput_.clear();
            curs_set(1);  // Show cursor
            break;

        // ── Tab: switch focus panel ──────────────────────────────
        case '\t':
            focusPanel_ = (focusPanel_ == FocusPanel::RESULTS)
                              ? FocusPanel::PLAYLIST
                              : FocusPanel::RESULTS;
            break;

        // ── Arrow navigation ─────────────────────────────────────
        case KEY_UP:
            if (focusPanel_ == FocusPanel::RESULTS) {
                if (selectedResult_ > 0) selectedResult_--;
            } else {
                if (selectedPlaylist_ > 0) selectedPlaylist_--;
            }
            break;

        case KEY_DOWN:
            if (focusPanel_ == FocusPanel::RESULTS) {
                if (selectedResult_ < static_cast<int>(searchResults_.size()) - 1)
                    selectedResult_++;
            } else {
                if (selectedPlaylist_ < static_cast<int>(playlist_.size()) - 1)
                    selectedPlaylist_++;
            }
            break;

        // ── Enter: play selected ─────────────────────────────────
        case '\n':
        case KEY_ENTER:
            if (focusPanel_ == FocusPanel::RESULTS) {
                playSelectedResult();
            } else {
                playSelectedPlaylistSong();
            }
            break;

        // ── Playback controls ────────────────────────────────────
        case ' ':  // Play/Pause
            if (player_.isPlaying() || player_.isPaused()) {
                player_.togglePause();
            }
            break;

        case 'n':
        case 'N':
            playNextSong();
            break;

        case 'p':
        case 'P':
            playPrevSong();
            break;

        case '+':
        case '=':
            player_.setVolume(player_.getVolume() + 5);
            setStatusMessage("Volume: " + std::to_string(player_.getVolume()) + "%");
            break;

        case '-':
        case '_':
            player_.setVolume(player_.getVolume() - 5);
            setStatusMessage("Volume: " + std::to_string(player_.getVolume()) + "%");
            break;

        // ── Playlist operations ──────────────────────────────────
        case 'a':
        case 'A':
            addSelectedToPlaylist();
            break;

        case 'd':
        case 'D':
        case KEY_DC:  // Delete key
            removeSelectedFromPlaylist();
            break;

        case 's':
        case 'S':
            mode_ = InputMode::SAVE_PROMPT;
            saveInput_ = playlist_.getPlaylistName();
            if (saveInput_ == "Untitled Playlist") saveInput_.clear();
            curs_set(1);
            break;

        case 'l':
        case 'L':
            savedPlaylists_ = playlist_.listPlaylists();
            if (savedPlaylists_.empty()) {
                setStatusMessage("No saved playlists found", true);
            } else {
                mode_ = InputMode::LOAD_LIST;
                selectedSavedPlaylist_ = 0;
            }
            break;

        // ── Clear search ─────────────────────────────────────────
        case 'c':
        case 'C':
            searchResults_.clear();
            selectedResult_ = 0;
            resultScrollOffset_ = 0;
            searchInput_.clear();
            setStatusMessage("Search results cleared");
            break;

        // ── Shuffle ──────────────────────────────────────────────
        case 'x':
        case 'X':
            playlist_.shuffle();
            setStatusMessage("Playlist shuffled");
            break;

        default:
            break;
    }
}

void TerminalUI::handleSearchInput(int ch) {
    switch (ch) {
        case 27:  // Escape
            mode_ = InputMode::NORMAL;
            curs_set(0);
            break;

        case '\n':
        case KEY_ENTER:
            if (!searchInput_.empty()) {
                curs_set(0);
                doSearch();
                mode_ = InputMode::NORMAL;
                focusPanel_ = FocusPanel::RESULTS;
            }
            break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            if (!searchInput_.empty()) {
                searchInput_.pop_back();
            }
            break;

        default:
            if (ch >= 32 && ch <= 126) {
                searchInput_ += static_cast<char>(ch);
            }
            break;
    }
}

void TerminalUI::handleSavePromptInput(int ch) {
    switch (ch) {
        case 27:  // Escape
            mode_ = InputMode::NORMAL;
            curs_set(0);
            break;

        case '\n':
        case KEY_ENTER:
            if (!saveInput_.empty()) {
                doSavePlaylist();
            }
            mode_ = InputMode::NORMAL;
            curs_set(0);
            break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            if (!saveInput_.empty()) {
                saveInput_.pop_back();
            }
            break;

        default:
            if (ch >= 32 && ch <= 126) {
                saveInput_ += static_cast<char>(ch);
            }
            break;
    }
}

void TerminalUI::handleLoadListInput(int ch) {
    switch (ch) {
        case 27:  // Escape
            mode_ = InputMode::NORMAL;
            break;

        case KEY_UP:
            if (selectedSavedPlaylist_ > 0) selectedSavedPlaylist_--;
            break;

        case KEY_DOWN:
            if (selectedSavedPlaylist_ < static_cast<int>(savedPlaylists_.size()) - 1)
                selectedSavedPlaylist_++;
            break;

        case '\n':
        case KEY_ENTER:
            doLoadPlaylist();
            mode_ = InputMode::NORMAL;
            break;

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────────────────────────────

void TerminalUI::drawAll() {
    // Check minimum terminal size
    if (termRows_ < MIN_ROWS || termCols_ < MIN_COLS) {
        werase(stdscr);
        mvprintw(termRows_ / 2, (termCols_ - 30) / 2,
                 "Terminal too small (%dx%d)", termCols_, termRows_);
        mvprintw(termRows_ / 2 + 1, (termCols_ - 30) / 2,
                 "Minimum: %dx%d", MIN_COLS, MIN_ROWS);
        refresh();
        return;
    }

    drawNowPlaying();
    drawSearchBox();
    drawResults();
    drawPlaylist();
    drawStatusBar();

    if (mode_ == InputMode::LOAD_LIST) {
        drawLoadList();
    }

    // Position cursor for text input modes
    if (mode_ == InputMode::SEARCH && winSearch_) {
        int curX = 10 + static_cast<int>(searchInput_.size());
        int maxX = getmaxx(winSearch_) - 2;
        if (curX > maxX) curX = maxX;
        wmove(winSearch_, 1, curX);
        wrefresh(winSearch_);
    } else if (mode_ == InputMode::SAVE_PROMPT && winPlaylist_) {
        // Cursor in the save prompt area
        wrefresh(winPlaylist_);
    }

    doupdate();
}

void TerminalUI::drawNowPlaying() {
    if (!winNowPlaying_) return;
    werase(winNowPlaying_);

    int w = getmaxx(winNowPlaying_);

    // Border
    wattron(winNowPlaying_, COLOR_PAIR(CP_ACCENT));
    box(winNowPlaying_, 0, 0);
    wattroff(winNowPlaying_, COLOR_PAIR(CP_ACCENT));

    // Title
    wattron(winNowPlaying_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvwprintw(winNowPlaying_, 0, 2, " 🎵 Now Playing ");
    wattroff(winNowPlaying_, COLOR_PAIR(CP_ACCENT) | A_BOLD);

    if (player_.isPlaying() || player_.isPaused()) {
        Song song = player_.getCurrentSong();
        double pos = player_.getPosition();
        double dur = player_.getDuration();

        if (dur <= 0 && song.duration > 0) dur = song.duration;

        // Equalizer animation logic
        if (player_.isPlaying()) {
            eqFrame_++;
            for (size_t i = 0; i < eqBars_.size(); ++i) {
                eqBars_[i] = ((eqFrame_ * (i + 1) * 7) % 4) + 1; // 1 to 3
            }
        } else {
            std::fill(eqBars_.begin(), eqBars_.end(), 1);
        }

        // Draw equalizer bars
        wattron(winNowPlaying_, COLOR_PAIR(CP_TITLE) | A_BOLD);
        for (size_t i = 0; i < eqBars_.size(); ++i) {
            for (int h = 0; h < 3; ++h) {
                if (h < eqBars_[i]) {
                    mvwprintw(winNowPlaying_, 3 - h, 2 + i, "█");
                }
            }
        }
        wattroff(winNowPlaying_, COLOR_PAIR(CP_TITLE) | A_BOLD);

        // Song title + artist
        std::string songInfo = song.title;
        if (!song.artist.empty()) songInfo += " — " + song.artist;
        songInfo = truncate(songInfo, w - 24 - 12); // -12 for eq offset

        wattron(winNowPlaying_, COLOR_PAIR(CP_PLAYING) | A_BOLD);
        mvwprintw(winNowPlaying_, 1, 12, "%s", songInfo.c_str());
        wattroff(winNowPlaying_, COLOR_PAIR(CP_PLAYING) | A_BOLD);

        // Time display
        std::string timeStr = Utils::formatTime(static_cast<int>(pos)) + " / " +
                              Utils::formatTime(static_cast<int>(dur));
        wattron(winNowPlaying_, COLOR_PAIR(CP_DIM));
        mvwprintw(winNowPlaying_, 1, w - static_cast<int>(timeStr.size()) - 2,
                  "%s", timeStr.c_str());
        wattroff(winNowPlaying_, COLOR_PAIR(CP_DIM));

        // Progress bar
        int barWidth = w - 22 - 10; 
        if (barWidth > 10) {
            drawProgressBar(winNowPlaying_, 2, 12, barWidth, pos, dur);
        }

        // Volume
        std::string volStr = "🔊 " + std::to_string(player_.getVolume()) + "%";
        mvwprintw(winNowPlaying_, 2, w - static_cast<int>(volStr.size()) - 2, "%s", volStr.c_str());

        // Play state indicator
        std::string state = player_.isPaused() ? "⏸ Paused" : "▶ Playing";
        wattron(winNowPlaying_, COLOR_PAIR(player_.isPaused() ? CP_HINT : CP_PLAYING));
        mvwprintw(winNowPlaying_, 3, 12, "%s", state.c_str());
        wattroff(winNowPlaying_, COLOR_PAIR(player_.isPaused() ? CP_HINT : CP_PLAYING));

    } else {
        wattron(winNowPlaying_, COLOR_PAIR(CP_DIM));
        mvwprintw(winNowPlaying_, 2, 12, "No song playing. Press / to search, then Enter to play.");
        wattroff(winNowPlaying_, COLOR_PAIR(CP_DIM));

        int barWidth = w - 16;
        if (barWidth > 10) {
            drawProgressBar(winNowPlaying_, 3, 12, barWidth, 0, 0);
        }
    }

    wnoutrefresh(winNowPlaying_);
}

void TerminalUI::drawSearchBox() {
    if (!winSearch_) return;
    werase(winSearch_);

    int w = getmaxx(winSearch_);
    int h = getmaxy(winSearch_);

    // Border (Highlight if active)
    int borderColor = (mode_ == InputMode::SEARCH || mode_ == InputMode::SAVE_PROMPT) ? CP_ACCENT : CP_BORDER;
    wattron(winSearch_, COLOR_PAIR(borderColor));
    box(winSearch_, 0, 0);
    wattroff(winSearch_, COLOR_PAIR(borderColor));

    // Title
    wattron(winSearch_, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwprintw(winSearch_, 0, 2, " Search ");
    wattroff(winSearch_, COLOR_PAIR(CP_TITLE) | A_BOLD);

    // Search input
    if (mode_ == InputMode::SEARCH) {
        wattron(winSearch_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
        mvwprintw(winSearch_, 1, 2, "Search: ");
        wattroff(winSearch_, COLOR_PAIR(CP_ACCENT) | A_BOLD);

        // Show typed text
        std::string displayText = searchInput_;
        int maxLen = w - 12;
        if (static_cast<int>(displayText.size()) > maxLen && maxLen > 0) {
            displayText = displayText.substr(displayText.size() - maxLen);
        }
        wattron(winSearch_, A_BOLD);
        mvwprintw(winSearch_, 1, 10, "%s", displayText.c_str());
        wattroff(winSearch_, A_BOLD);

        // Hint
        wattron(winSearch_, COLOR_PAIR(CP_DIM));
        mvwprintw(winSearch_, h - 2, 2, "Enter=Search  Esc=Cancel");
        wattroff(winSearch_, COLOR_PAIR(CP_DIM));
    } else if (mode_ == InputMode::SAVE_PROMPT) {
        wattron(winSearch_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
        mvwprintw(winSearch_, 1, 2, "Save as: ");
        wattroff(winSearch_, COLOR_PAIR(CP_ACCENT) | A_BOLD);

        wattron(winSearch_, A_BOLD);
        std::string displayText = saveInput_;
        int maxLen = w - 13;
        if (static_cast<int>(displayText.size()) > maxLen && maxLen > 0) {
            displayText = displayText.substr(displayText.size() - maxLen);
        }
        mvwprintw(winSearch_, 1, 11, "%s_", displayText.c_str());
        wattroff(winSearch_, A_BOLD);

        wattron(winSearch_, COLOR_PAIR(CP_DIM));
        mvwprintw(winSearch_, h - 2, 2, "Enter=Save  Esc=Cancel");
        wattroff(winSearch_, COLOR_PAIR(CP_DIM));
    } else {
        // Show last search query or hint
        wattron(winSearch_, COLOR_PAIR(CP_DIM));
        mvwprintw(winSearch_, 1, 2, "Search: ");
        if (!searchInput_.empty()) {
            mvwprintw(winSearch_, 1, 10, "%s", truncate(searchInput_, w - 12).c_str());
        }
        wattroff(winSearch_, COLOR_PAIR(CP_DIM));

        if (searching_) {
            wattron(winSearch_, COLOR_PAIR(CP_HINT) | A_BOLD);
            mvwprintw(winSearch_, h - 2, 2, "Searching...");
            wattroff(winSearch_, COLOR_PAIR(CP_HINT) | A_BOLD);
        } else {
            wattron(winSearch_, COLOR_PAIR(CP_DIM));
            mvwprintw(winSearch_, h - 2, 2, "Press / to search");
            wattroff(winSearch_, COLOR_PAIR(CP_DIM));
        }
    }

    // Status message
    if (!statusMessage_.empty() && statusTimer_ > 0) {
        int msgColor = statusIsError_ ? CP_ERROR : CP_PLAYING;
        wattron(winSearch_, COLOR_PAIR(msgColor) | A_BOLD);
        int msgY = h - 3;
        if (msgY < 2) msgY = 2;
        mvwprintw(winSearch_, msgY, 2, "%s",
                  truncate(statusMessage_, w - 4).c_str());
        wattroff(winSearch_, COLOR_PAIR(msgColor) | A_BOLD);
    }

    wnoutrefresh(winSearch_);
}

void TerminalUI::drawResults() {
    if (!winResults_) return;
    werase(winResults_);

    int w = getmaxx(winResults_);
    int h = getmaxy(winResults_);

    // Border (Highlight if active)
    int borderColor = (focusPanel_ == FocusPanel::RESULTS && mode_ == InputMode::NORMAL) ? CP_ACCENT : CP_BORDER;
    wattron(winResults_, COLOR_PAIR(borderColor));
    box(winResults_, 0, 0);
    wattroff(winResults_, COLOR_PAIR(borderColor));

    // Title — highlight if focused
    if (focusPanel_ == FocusPanel::RESULTS) {
        wattron(winResults_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
        mvwprintw(winResults_, 0, 2, " Results [%zu] ",
                  searchResults_.size());
        wattroff(winResults_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
    } else {
        wattron(winResults_, COLOR_PAIR(CP_TITLE) | A_BOLD);
        mvwprintw(winResults_, 0, 2, " Results [%zu] ",
                  searchResults_.size());
        wattroff(winResults_, COLOR_PAIR(CP_TITLE) | A_BOLD);
    }

    if (searchResults_.empty()) {
        wattron(winResults_, COLOR_PAIR(CP_DIM));
        mvwprintw(winResults_, h / 2, (w - 16) / 2, "No results yet");
        wattroff(winResults_, COLOR_PAIR(CP_DIM));
        wnoutrefresh(winResults_);
        return;
    }

    int visibleRows = h - 2;  // Exclude top and bottom border
    if (visibleRows <= 0) {
        wnoutrefresh(winResults_);
        return;
    }

    // Adjust scroll offset to keep selected item visible
    if (selectedResult_ < resultScrollOffset_) {
        resultScrollOffset_ = selectedResult_;
    }
    if (selectedResult_ >= resultScrollOffset_ + visibleRows) {
        resultScrollOffset_ = selectedResult_ - visibleRows + 1;
    }

    for (int i = 0; i < visibleRows; ++i) {
        int idx = resultScrollOffset_ + i;
        if (idx >= static_cast<int>(searchResults_.size())) break;

        const Song& song = searchResults_[idx];
        bool isSelected = (idx == selectedResult_ && focusPanel_ == FocusPanel::RESULTS);

        // Format: "N. Title — Artist  (MM:SS)"
        std::string durStr = "(" + Utils::formatTime(song.duration) + ")";
        std::string prefix = std::to_string(idx + 1) + ". ";
        std::string info = song.title;
        if (!song.artist.empty()) info += " — " + song.artist;

        int availWidth = w - 4 - static_cast<int>(durStr.size()) -
                         static_cast<int>(prefix.size()) - 1;
        if (availWidth > 0) {
            info = truncate(info, availWidth);
        }

        std::string line = prefix + info;
        // Pad to fill width for highlight
        int lineWidth = w - 4 - static_cast<int>(durStr.size()) - 1;
        while (static_cast<int>(line.size()) < lineWidth) line += ' ';
        line += " " + durStr;

        if (isSelected) {
            wattron(winResults_, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        }

        mvwprintw(winResults_, 1 + i, 2, "%s", truncate(line, w - 4).c_str());

        if (isSelected) {
            wattroff(winResults_, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        }
    }

    // Scroll indicators
    if (resultScrollOffset_ > 0) {
        wattron(winResults_, COLOR_PAIR(CP_HINT));
        mvwprintw(winResults_, 0, w - 5, " ▲ ");
        wattroff(winResults_, COLOR_PAIR(CP_HINT));
    }
    if (resultScrollOffset_ + visibleRows < static_cast<int>(searchResults_.size())) {
        wattron(winResults_, COLOR_PAIR(CP_HINT));
        mvwprintw(winResults_, h - 1, w - 5, " ▼ ");
        wattroff(winResults_, COLOR_PAIR(CP_HINT));
    }

    wnoutrefresh(winResults_);
}

void TerminalUI::drawPlaylist() {
    if (!winPlaylist_) return;
    werase(winPlaylist_);

    int w = getmaxx(winPlaylist_);
    int h = getmaxy(winPlaylist_);

    // Border (Highlight if active)
    int borderColor = (focusPanel_ == FocusPanel::PLAYLIST && mode_ == InputMode::NORMAL) ? CP_ACCENT : CP_BORDER;
    wattron(winPlaylist_, COLOR_PAIR(borderColor));
    box(winPlaylist_, 0, 0);
    wattroff(winPlaylist_, COLOR_PAIR(borderColor));

    // Title — highlight if focused
    std::string title = " Playlist: " + playlist_.getPlaylistName() +
                        " (" + std::to_string(playlist_.size()) + " songs) ";
    title = truncate(title, w - 4);

    if (focusPanel_ == FocusPanel::PLAYLIST) {
        wattron(winPlaylist_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
    } else {
        wattron(winPlaylist_, COLOR_PAIR(CP_TITLE) | A_BOLD);
    }
    mvwprintw(winPlaylist_, 0, 2, "%s", title.c_str());
    if (focusPanel_ == FocusPanel::PLAYLIST) {
        wattroff(winPlaylist_, COLOR_PAIR(CP_ACCENT) | A_BOLD);
    } else {
        wattroff(winPlaylist_, COLOR_PAIR(CP_TITLE) | A_BOLD);
    }

    const auto& songs = playlist_.getSongs();

    if (songs.empty()) {
        wattron(winPlaylist_, COLOR_PAIR(CP_DIM));
        mvwprintw(winPlaylist_, h / 2, (w - 26) / 2,
                  "Playlist is empty (A=Add)");
        wattroff(winPlaylist_, COLOR_PAIR(CP_DIM));
        wnoutrefresh(winPlaylist_);
        return;
    }

    int visibleRows = h - 2;
    if (visibleRows <= 0) {
        wnoutrefresh(winPlaylist_);
        return;
    }

    // Adjust scroll
    if (selectedPlaylist_ < playlistScrollOffset_) {
        playlistScrollOffset_ = selectedPlaylist_;
    }
    if (selectedPlaylist_ >= playlistScrollOffset_ + visibleRows) {
        playlistScrollOffset_ = selectedPlaylist_ - visibleRows + 1;
    }

    for (int i = 0; i < visibleRows; ++i) {
        int idx = playlistScrollOffset_ + i;
        if (idx >= static_cast<int>(songs.size())) break;

        const Song& song = songs[idx];
        bool isSelected = (idx == selectedPlaylist_ && focusPanel_ == FocusPanel::PLAYLIST);
        bool isCurrentlyPlaying = (idx == playlist_.getCurrentIndex() &&
                                   (player_.isPlaying() || player_.isPaused()));

        std::string durStr = "(" + Utils::formatTime(song.duration) + ")";
        std::string prefix = std::to_string(idx + 1) + ". ";
        if (isCurrentlyPlaying) prefix = "▶ " + prefix;

        std::string info = song.title;
        if (!song.artist.empty()) info += " — " + song.artist;

        int availWidth = w - 4 - static_cast<int>(durStr.size()) -
                         static_cast<int>(prefix.size()) - 1;
        if (availWidth > 0) {
            info = truncate(info, availWidth);
        }

        std::string line = prefix + info;
        int lineWidth = w - 4 - static_cast<int>(durStr.size()) - 1;
        while (static_cast<int>(line.size()) < lineWidth) line += ' ';
        line += " " + durStr;

        if (isSelected) {
            wattron(winPlaylist_, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        } else if (isCurrentlyPlaying) {
            wattron(winPlaylist_, COLOR_PAIR(CP_PLAYING) | A_BOLD);
        }

        mvwprintw(winPlaylist_, 1 + i, 2, "%s", truncate(line, w - 4).c_str());

        if (isSelected) {
            wattroff(winPlaylist_, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        } else if (isCurrentlyPlaying) {
            wattroff(winPlaylist_, COLOR_PAIR(CP_PLAYING) | A_BOLD);
        }
    }

    // Scroll indicators
    if (playlistScrollOffset_ > 0) {
        wattron(winPlaylist_, COLOR_PAIR(CP_HINT));
        mvwprintw(winPlaylist_, 0, w - 5, " ▲ ");
        wattroff(winPlaylist_, COLOR_PAIR(CP_HINT));
    }
    if (playlistScrollOffset_ + visibleRows < static_cast<int>(songs.size())) {
        wattron(winPlaylist_, COLOR_PAIR(CP_HINT));
        mvwprintw(winPlaylist_, h - 1, w - 5, " ▼ ");
        wattroff(winPlaylist_, COLOR_PAIR(CP_HINT));
    }

    wnoutrefresh(winPlaylist_);
}

void TerminalUI::drawStatusBar() {
    if (!winStatus_) return;
    werase(winStatus_);

    int w = getmaxx(winStatus_);

    // Border
    wattron(winStatus_, COLOR_PAIR(CP_BORDER));
    box(winStatus_, 0, 0);
    wattroff(winStatus_, COLOR_PAIR(CP_BORDER));

    wattron(winStatus_, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwprintw(winStatus_, 0, 2, " Keys ");
    wattroff(winStatus_, COLOR_PAIR(CP_TITLE) | A_BOLD);

    // Build shortcuts string based on current mode
    std::string hints;
    if (mode_ == InputMode::SEARCH) {
        hints = "Type to search | Enter=Search | Esc=Cancel";
    } else if (mode_ == InputMode::SAVE_PROMPT) {
        hints = "Type playlist name | Enter=Save | Esc=Cancel";
    } else if (mode_ == InputMode::LOAD_LIST) {
        hints = "↑/↓=Select | Enter=Load | Esc=Cancel";
    } else {
        hints = "/=Search  Space=Play/Pause  N=Next  P=Prev  +=Vol+  -=Vol-  "
                "A=Add  D=Del  S=Save  L=Load  Tab=Switch  X=Shuffle  Q=Quit";
    }

    wattron(winStatus_, COLOR_PAIR(CP_HINT));
    mvwprintw(winStatus_, 1, 2, "%s", truncate(hints, w - 4).c_str());
    wattroff(winStatus_, COLOR_PAIR(CP_HINT));

    wnoutrefresh(winStatus_);
}

void TerminalUI::drawLoadList() {
    // Overlay window in the center of the screen
    int overlayH = std::min(static_cast<int>(savedPlaylists_.size()) + 4, termRows_ - 4);
    int overlayW = std::min(40, termCols_ - 4);
    int startY = (termRows_ - overlayH) / 2;
    int startX = (termCols_ - overlayW) / 2;

    WINDOW* overlay = newwin(overlayH, overlayW, startY, startX);
    if (!overlay) return;

    // Border and title
    wattron(overlay, COLOR_PAIR(CP_BORDER));
    box(overlay, 0, 0);
    wattroff(overlay, COLOR_PAIR(CP_BORDER));

    wattron(overlay, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwprintw(overlay, 0, 2, " Load Playlist ");
    wattroff(overlay, COLOR_PAIR(CP_TITLE) | A_BOLD);

    wattron(overlay, COLOR_PAIR(CP_DIM));
    mvwprintw(overlay, overlayH - 1, 2, " ↑/↓ Enter Esc ");
    wattroff(overlay, COLOR_PAIR(CP_DIM));

    // List playlists
    int visibleRows = overlayH - 3;
    int scrollOff = 0;
    if (selectedSavedPlaylist_ >= visibleRows) {
        scrollOff = selectedSavedPlaylist_ - visibleRows + 1;
    }

    for (int i = 0; i < visibleRows; ++i) {
        int idx = scrollOff + i;
        if (idx >= static_cast<int>(savedPlaylists_.size())) break;

        bool isSelected = (idx == selectedSavedPlaylist_);

        if (isSelected) {
            wattron(overlay, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        }

        std::string displayName = truncate(savedPlaylists_[idx], overlayW - 6);
        std::string line = "  " + displayName;
        while (static_cast<int>(line.size()) < overlayW - 2) line += ' ';

        mvwprintw(overlay, 1 + i, 1, "%s", line.c_str());

        if (isSelected) {
            wattroff(overlay, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        }
    }

    wrefresh(overlay);
    delwin(overlay);
}

void TerminalUI::drawProgressBar(WINDOW* win, int y, int x, int width,
                                  double current, double total) {
    if (width <= 0) return;

    double ratio = (total > 0) ? (current / total) : 0.0;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    int filled = static_cast<int>(ratio * width);

    wmove(win, y, x);

    // Filled portion
    wattron(win, COLOR_PAIR(CP_BAR_FILL));
    for (int i = 0; i < filled; ++i) {
        wprintw(win, "█");
    }
    wattroff(win, COLOR_PAIR(CP_BAR_FILL));

    // Empty portion
    wattron(win, COLOR_PAIR(CP_BAR_EMPTY) | A_DIM);
    for (int i = filled; i < width; ++i) {
        wprintw(win, "░");
    }
    wattroff(win, COLOR_PAIR(CP_BAR_EMPTY) | A_DIM);
}

// ─────────────────────────────────────────────────────────────────────
// Window Management
// ─────────────────────────────────────────────────────────────────────

void TerminalUI::createWindows() {
    recalculateLayout();
}

void TerminalUI::destroyWindows() {
    if (winNowPlaying_) { delwin(winNowPlaying_); winNowPlaying_ = nullptr; }
    if (winSearch_)     { delwin(winSearch_);     winSearch_     = nullptr; }
    if (winResults_)    { delwin(winResults_);    winResults_    = nullptr; }
    if (winPlaylist_)   { delwin(winPlaylist_);   winPlaylist_   = nullptr; }
    if (winStatus_)     { delwin(winStatus_);     winStatus_     = nullptr; }
}

void TerminalUI::recalculateLayout() {
    destroyWindows();

    getmaxyx(stdscr, termRows_, termCols_);

    if (termRows_ < MIN_ROWS || termCols_ < MIN_COLS) return;

    // Layout allocation:
    // Row 0..(NPH-1)           → Now Playing  (4 rows)
    // Row NPH..(middleEnd)     → Search (left ~30%) + Results (right ~70%)
    // Row (middleEnd+1)..(SBstart-1) → Playlist
    // Row (SBstart)..(end)     → Status Bar  (3 rows)

    int middleHeight = (termRows_ - NOW_PLAYING_HEIGHT - STATUS_BAR_HEIGHT) / 2;
    if (middleHeight < 4) middleHeight = 4;
    int playlistHeight = termRows_ - NOW_PLAYING_HEIGHT - middleHeight - STATUS_BAR_HEIGHT;
    if (playlistHeight < 3) playlistHeight = 3;

    int searchWidth = termCols_ / 3;
    if (searchWidth < 20) searchWidth = 20;
    if (searchWidth > 40) searchWidth = 40;
    int resultsWidth = termCols_ - searchWidth;

    // Create windows
    winNowPlaying_ = newwin(NOW_PLAYING_HEIGHT, termCols_, 0, 0);
    winSearch_     = newwin(middleHeight, searchWidth, NOW_PLAYING_HEIGHT, 0);
    winResults_    = newwin(middleHeight, resultsWidth, NOW_PLAYING_HEIGHT, searchWidth);
    winPlaylist_   = newwin(playlistHeight, termCols_, NOW_PLAYING_HEIGHT + middleHeight, 0);
    winStatus_     = newwin(STATUS_BAR_HEIGHT, termCols_,
                            termRows_ - STATUS_BAR_HEIGHT, 0);

    // Enable keypad for all windows
    if (winNowPlaying_) keypad(winNowPlaying_, TRUE);
    if (winSearch_)     keypad(winSearch_, TRUE);
    if (winResults_)    keypad(winResults_, TRUE);
    if (winPlaylist_)   keypad(winPlaylist_, TRUE);
    if (winStatus_)     keypad(winStatus_, TRUE);
}

// ─────────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────────

void TerminalUI::doSearch() {
    if (searchInput_.empty()) return;

    setStatusMessage("Searching YouTube...");
    searching_ = true;
    drawAll();

    searchResults_ = searcher_.search(searchInput_, 10);
    searching_ = false;

    if (searchResults_.empty()) {
        std::string err = searcher_.getLastError();
        if (err.empty()) err = "No results found for: " + searchInput_;
        setStatusMessage(err, true);
    } else {
        setStatusMessage("Found " + std::to_string(searchResults_.size()) + " results");
        selectedResult_ = 0;
        resultScrollOffset_ = 0;
    }
}

void TerminalUI::playSelectedResult() {
    if (searchResults_.empty() || selectedResult_ < 0 ||
        selectedResult_ >= static_cast<int>(searchResults_.size())) {
        return;
    }

    const Song& song = searchResults_[selectedResult_];
    setStatusMessage("Loading stream: " + song.title);
    drawAll();

    std::string streamURL = searcher_.getStreamURL(song.id);
    if (streamURL.empty()) {
        setStatusMessage("Failed to get stream URL", true);
        return;
    }

    if (player_.play(song, streamURL)) {
        setStatusMessage("Now playing: " + song.title);
    } else {
        setStatusMessage("Playback failed: " + player_.getLastError(), true);
    }
}

void TerminalUI::playSelectedPlaylistSong() {
    if (playlist_.size() == 0 || selectedPlaylist_ < 0 ||
        selectedPlaylist_ >= static_cast<int>(playlist_.size())) {
        return;
    }

    playlist_.setCurrentIndex(selectedPlaylist_);
    const Song& song = playlist_.getSongAt(selectedPlaylist_);
    setStatusMessage("Loading stream: " + song.title);
    drawAll();

    std::string streamURL = searcher_.getStreamURL(song.id);
    if (streamURL.empty()) {
        setStatusMessage("Failed to get stream URL", true);
        return;
    }

    if (player_.play(song, streamURL)) {
        setStatusMessage("Now playing: " + song.title);
    } else {
        setStatusMessage("Playback failed: " + player_.getLastError(), true);
    }
}

void TerminalUI::addSelectedToPlaylist() {
    if (searchResults_.empty() || selectedResult_ < 0 ||
        selectedResult_ >= static_cast<int>(searchResults_.size())) {
        setStatusMessage("No result selected to add", true);
        return;
    }

    const Song& song = searchResults_[selectedResult_];
    playlist_.addSong(song);
    setStatusMessage("Added: " + song.title);
}

void TerminalUI::removeSelectedFromPlaylist() {
    if (focusPanel_ != FocusPanel::PLAYLIST) {
        setStatusMessage("Switch to playlist (Tab) to delete", true);
        return;
    }

    if (playlist_.size() == 0 || selectedPlaylist_ < 0 ||
        selectedPlaylist_ >= static_cast<int>(playlist_.size())) {
        setStatusMessage("No song selected to remove", true);
        return;
    }

    std::string name = playlist_.getSongAt(selectedPlaylist_).title;
    playlist_.removeSong(selectedPlaylist_);

    // Adjust selection
    if (selectedPlaylist_ >= static_cast<int>(playlist_.size()) && selectedPlaylist_ > 0) {
        selectedPlaylist_--;
    }

    setStatusMessage("Removed: " + name);
}

void TerminalUI::doSavePlaylist() {
    if (saveInput_.empty()) {
        setStatusMessage("Playlist name cannot be empty", true);
        return;
    }

    playlist_.setPlaylistName(saveInput_);
    if (playlist_.savePlaylist(saveInput_)) {
        setStatusMessage("Playlist saved: " + saveInput_);
    } else {
        setStatusMessage("Save failed: " + playlist_.getLastError(), true);
    }
}

void TerminalUI::doLoadPlaylist() {
    if (savedPlaylists_.empty() || selectedSavedPlaylist_ < 0 ||
        selectedSavedPlaylist_ >= static_cast<int>(savedPlaylists_.size())) {
        return;
    }

    std::string name = savedPlaylists_[selectedSavedPlaylist_];
    if (playlist_.loadPlaylist(name)) {
        setStatusMessage("Loaded playlist: " + name);
        selectedPlaylist_ = 0;
        playlistScrollOffset_ = 0;
    } else {
        setStatusMessage("Load failed: " + playlist_.getLastError(), true);
    }
}

void TerminalUI::playNextSong() {
    if (playlist_.size() == 0) {
        setStatusMessage("Playlist is empty", true);
        return;
    }

    Song song = playlist_.nextSong();
    selectedPlaylist_ = playlist_.getCurrentIndex();
    setStatusMessage("Loading: " + song.title);
    drawAll();

    std::string streamURL = searcher_.getStreamURL(song.id);
    if (streamURL.empty()) {
        setStatusMessage("Failed to get stream URL for next song", true);
        return;
    }

    if (player_.play(song, streamURL)) {
        setStatusMessage("Now playing: " + song.title);
    } else {
        setStatusMessage("Playback failed", true);
    }
}

void TerminalUI::playPrevSong() {
    if (playlist_.size() == 0) {
        setStatusMessage("Playlist is empty", true);
        return;
    }

    Song song = playlist_.prevSong();
    selectedPlaylist_ = playlist_.getCurrentIndex();
    setStatusMessage("Loading: " + song.title);
    drawAll();

    std::string streamURL = searcher_.getStreamURL(song.id);
    if (streamURL.empty()) {
        setStatusMessage("Failed to get stream URL for previous song", true);
        return;
    }

    if (player_.play(song, streamURL)) {
        setStatusMessage("Now playing: " + song.title);
    } else {
        setStatusMessage("Playback failed", true);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────

std::string TerminalUI::truncate(const std::string& str, size_t maxLen) const {
    if (maxLen == 0) return "";
    if (str.size() <= maxLen) return str;
    if (maxLen <= 3) return str.substr(0, maxLen);
    return str.substr(0, maxLen - 3) + "...";
}

void TerminalUI::setStatusMessage(const std::string& msg, bool isError) {
    statusMessage_ = msg;
    statusIsError_ = isError;
    statusTimer_ = isError ? 100 : 60;  // ~5s for errors, ~3s for info at 20fps
}
