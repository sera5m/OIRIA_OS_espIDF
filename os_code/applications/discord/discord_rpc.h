#pragma once
// discord_rpc — webhook alerts from watch (sensors / pools / plain text)
//
// Drop under e.g. os_code/core/com/ and add discord_rpc.cpp to CMake SRCS.
// REQUIRES: esp_http_client, (esp-tls), nvs_flash, esp_wifi, freertos

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DISCORD_RPC_CONTENT_MAX
#define DISCORD_RPC_CONTENT_MAX 256
#endif

// Optional runtime override (null-terminated). If empty, uses built-in default webhook.
extern char USR_DISCORD_WEBHOOK[256];

// Set / clear user webhook (copies, truncates to 255).
void discord_rpc_set_webhook(const char* url);

// Plain text (spawns 16KB worker: ensure WiFi → POST). Non-blocking.
void send_discord_rpc(void);
void send_discord_rpc_msg(const char* content);

// Same, but copies up to len bytes (not requiring NUL); adds NUL internally.
void send_discord_rpc_buf(const char* data, size_t len);

// Format minimal CBOR (ints/text/bool/null/arrays/maps) into a Discord message and send.
// Unknown major types → hex dump of remaining bytes. Returns false if empty payload.
bool send_discord_rpc_cbor(const uint8_t* cbor, size_t cbor_len);

// Helper only: write human-readable text into out (NUL-terminated). Returns bytes written.
size_t cbor_to_discord_msg(const uint8_t* cbor, size_t cbor_len, char* out, size_t out_cap);

// Best-effort: delete in-flight worker if still running. Returns true if a task was deleted.
bool despawn_discord_rpc(void);

#ifdef __cplusplus
}
#endif
