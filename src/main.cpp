#include "ui.h"
#include "youtube_searcher.h"
#include "audio_player.h"
#include "playlist_manager.h"
#include "utils.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

// Global flag for signal handling — allows clean ncurses shutdown
static volatile sig_atomic_t g_interrupted = 0;

static void signalHandler(int sig) {
    g_interrupted = 1;
}

int main(int argc, char* argv[]) {
    // Ensure the playlist directory exists on first run
    Utils::getPlaylistDir();

    // Set up signal handlers for graceful shutdown
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Ignore SIGPIPE (can happen if mpv pipe breaks)
    signal(SIGPIPE, SIG_IGN);

    // Create core components
    YouTubeSearcher searcher;
    AudioPlayer player;
    PlaylistManager playlist;

    // Create and run the terminal UI
    TerminalUI ui(searcher, player, playlist);

    try {
        ui.init();
        ui.run();
    } catch (const std::exception& e) {
        ui.cleanup();
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        ui.cleanup();
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }

    ui.cleanup();

    // Stop any lingering playback
    player.stop();

    return 0;
}
