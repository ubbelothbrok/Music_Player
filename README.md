# 🎵 YouTube Terminal Music Player

A fully-featured terminal-based music player built in C++17 that searches YouTube, streams audio directly (no files downloaded), manages playlists, and provides a rich ncurses UI — all from your terminal.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green)

## ✨ Features

- **YouTube Search** — Search for songs by title, artist, or keywords directly from the terminal
- **Audio Streaming** — Stream audio from YouTube without downloading files to disk
- **Playlist Management** — Create, save, load, and shuffle playlists stored as JSON
- **Rich Terminal UI** — ncurses-based interface with color-coded panels, progress bar, and scrollable lists
- **Keyboard-Driven** — Full keyboard controls for navigation, playback, and playlist management
- **Search Result Caching** — Avoids duplicate API calls for repeated searches

## 📋 Prerequisites

### macOS (Homebrew)
```bash
brew install cmake yt-dlp ffmpeg mpv ncurses
```

### Linux (apt)
```bash
sudo apt update
sudo apt install cmake g++ libncursesw5-dev libcurl4-openssl-dev yt-dlp mpv ffmpeg
```

> **Note:** `nlohmann/json` is automatically downloaded during CMake configuration via FetchContent.

## 🔨 Building

```bash
cd Music_Player
mkdir build && cd build
cmake ..
make -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
```

The executable `music_player` will be created in the `build/` directory.

## 🚀 Running

```bash
./music_player
```

The application will:
1. Create `~/.music_playlists/` directory automatically on first run
2. Display the terminal UI with empty results and playlist
3. Wait for your input

## ⌨️ Keyboard Controls

### Navigation
| Key | Action |
|-----|--------|
| `Tab` | Switch focus between Results and Playlist panels |
| `↑ / ↓` | Navigate items in the focused panel |

### Search
| Key | Action |
|-----|--------|
| `/` | Open search mode (type your query) |
| `Enter` | Execute search / play selected song |
| `Esc` | Cancel search / exit current mode |
| `C` | Clear search results |

### Playback
| Key | Action |
|-----|--------|
| `Space` | Play / Pause |
| `N` | Next song in playlist |
| `P` | Previous song in playlist |
| `+` / `=` | Volume up (+5%) |
| `-` / `_` | Volume down (-5%) |

### Playlist
| Key | Action |
|-----|--------|
| `A` | Add selected search result to playlist |
| `D` / `Del` | Remove selected song from playlist |
| `S` | Save playlist (prompts for name) |
| `L` | Load a saved playlist |
| `X` | Shuffle playlist |

### Utility
| Key | Action |
|-----|--------|
| `Q` | Quit application |

## 📐 UI Layout

```
┌─────────────────────── Now Playing ──────────────────────────┐
│ ♫ Song Title — Artist                     03:24 / 05:12     │
│ ████████████████░░░░░░░░░░░░   Vol:80%   [▶ Playing]        │
├────── Search ──────┬──────────── Results ────────────────────┤
│ Search: query      │ 1. Song Name — Artist         (5:55)   │
│                    │ 2. Another Song — Artist       (3:12)   │
│ Press / to search  │ ▸ 3. Selected Song            (4:30)   │
├────────────────────┴──────── Playlist ──────────────────────┤
│ Playlist: My Favorites (3 songs)                            │
│ ▶ 1. Now Playing Song — Artist                       (3:24) │
│   2. Another Song — Artist                           (4:01) │
├──────────────────── Keys ──────────────────────────────────┤
│ /=Search  Space=Play/Pause  N=Next  A=Add  S=Save  Q=Quit  │
└─────────────────────────────────────────────────────────────┘
```

## 📁 Project Structure

```
Music_Player/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── audio_player.h
│   ├── playlist_manager.h
│   ├── ui.h
│   ├── utils.h
│   └── youtube_searcher.h
└── src/
    ├── audio_player.cpp
    ├── main.cpp
    ├── playlist_manager.cpp
    ├── ui.cpp
    ├── utils.cpp
    └── youtube_searcher.cpp
```

## 💾 Data Storage

Playlists are saved as JSON files in `~/.music_playlists/`:

```json
{
  "name": "My Playlist",
  "created": "2026-08-08T19:30:00",
  "songs": [
    {
      "id": "dQw4w9WgXcQ",
      "title": "Rick Astley - Never Gonna Give You Up",
      "artist": "Rick Astley",
      "duration": 213,
      "url": "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
    }
  ]
}
```

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| `yt-dlp: command not found` | Install yt-dlp: `brew install yt-dlp` or `pip install yt-dlp` |
| `mpv: command not found` | Install mpv: `brew install mpv` or `sudo apt install mpv` |
| No sound plays | Check that mpv works: `mpv --no-video <youtube-url>` |
| Search returns no results | Check internet connection; yt-dlp may need updating: `yt-dlp -U` |
| Build error about ncurses | Install ncurses dev package: `brew install ncurses` or `sudo apt install libncursesw5-dev` |
| Terminal too small error | Resize terminal to at least 60×18 characters |

## 🛠️ Technology Stack

- **Language:** C++17
- **Build System:** CMake 3.16+
- **Terminal UI:** ncurses (wide-character)
- **Audio Playback:** mpv (via IPC socket)
- **YouTube Integration:** yt-dlp
- **JSON Parsing:** nlohmann/json
- **Networking:** libcurl
