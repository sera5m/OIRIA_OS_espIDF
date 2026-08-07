#pragma once
// ---------------------------------------------------------------------------
// Shared high-score / game-stats persistence for mini-games.
//
// On-disk layout (binary, little-endian):
//   /sdcard/apps/savedat/<AppName>_gamestats.rgs
//
// Format version 1:
//   magic[4] = 'R','G','S','1'
//   uint32_t version = 1
//   uint32_t high_score
//   uint32_t games_played
//   uint32_t last_score
//   uint32_t reserved[4]
//
// Uses POSIX open/read/write against the SD mount (d_sdc / VFS).
// Falls back silently if the card is missing – games still run.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "esp_log.h"

struct GameStats {
    uint32_t high_score   = 0;
    uint32_t games_played = 0;
    uint32_t last_score   = 0;
};

namespace game_stats {

static const char* TAG = "GameStats";
static constexpr char kMagic[4] = {'R', 'G', 'S', '1'};
static constexpr uint32_t kVersion = 1;

// Ensure /sdcard/apps/savedat exists (best-effort).
inline void ensure_dirs() {
    mkdir("/sdcard/apps", 0755);
    mkdir("/sdcard/apps/savedat", 0755);
}

// Build path: /sdcard/apps/savedat/<app>_gamestats.rgs
inline void make_path(char* out, size_t out_len, const char* app_name) {
    snprintf(out, out_len, "/sdcard/apps/savedat/%s_gamestats.rgs",
             app_name ? app_name : "game");
}

inline bool load(const char* app_name, GameStats& out) {
    out = GameStats{};
    ensure_dirs();

    char path[128];
    make_path(path, sizeof(path), app_name);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGD(TAG, "No stats file yet (%s): %s", path, strerror(errno));
        return false;
    }

    char magic[4] = {0};
    uint32_t ver = 0;
    uint32_t fields[7] = {0}; // high, games, last, reserved[4]

    bool ok = true;
    if (read(fd, magic, 4) != 4) ok = false;
    if (ok && read(fd, &ver, 4) != 4) ok = false;
    if (ok && read(fd, fields, sizeof(fields)) != (ssize_t)sizeof(fields)) ok = false;
    close(fd);

    if (!ok || memcmp(magic, kMagic, 4) != 0 || ver != kVersion) {
        ESP_LOGW(TAG, "Corrupt or unknown stats file: %s", path);
        return false;
    }

    out.high_score   = fields[0];
    out.games_played = fields[1];
    out.last_score   = fields[2];
    ESP_LOGI(TAG, "Loaded %s: high=%u played=%u last=%u",
             app_name, (unsigned)out.high_score,
             (unsigned)out.games_played, (unsigned)out.last_score);
    return true;
}

inline bool save(const char* app_name, const GameStats& st) {
    ensure_dirs();

    char path[128];
    make_path(path, sizeof(path), app_name);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ESP_LOGW(TAG, "Cannot write %s: %s", path, strerror(errno));
        return false;
    }

    uint32_t ver = kVersion;
    uint32_t fields[7] = {
        st.high_score,
        st.games_played,
        st.last_score,
        0, 0, 0, 0
    };

    bool ok = true;
    if (write(fd, kMagic, 4) != 4) ok = false;
    if (ok && write(fd, &ver, 4) != 4) ok = false;
    if (ok && write(fd, fields, sizeof(fields)) != (ssize_t)sizeof(fields)) ok = false;
    close(fd);

    if (ok) {
        ESP_LOGI(TAG, "Saved %s: high=%u played=%u last=%u",
                 app_name, (unsigned)st.high_score,
                 (unsigned)st.games_played, (unsigned)st.last_score);
    }
    return ok;
}

// Record end-of-round: bumps games_played, updates last/high, persists.
inline void record_round(const char* app_name, GameStats& st, uint32_t score) {
    st.games_played += 1;
    st.last_score = score;
    if (score > st.high_score)
        st.high_score = score;
    save(app_name, st);
}

} // namespace game_stats
