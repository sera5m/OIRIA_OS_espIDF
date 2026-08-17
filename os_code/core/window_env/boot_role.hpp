#pragma once
// =============================================================================
// boot_role – same firmware, different final-boot personality
// =============================================================================
// Roles (Linux-server analogy):
//
//   BOOT_ROLE_TYRANT  – has (or is) the panel. Receives DomFrames over UART,
//                       applies them, drives LCD. May command puppets
//                       (open app X, pause, etc.) via RSDOM control packets.
//
//   BOOT_ROLE_PUPPET  – headless. Boots the same appManager + apps, never
//                       touches LCD. After each tick, packs DomFrame and TX.
//
//   BOOT_ROLE_SOLO    – classic single-chip: local apps + local LCD (default).
//
// Selection (first match wins):
//   1) Kconfig / compile flag  CONFIG_RS_BOOT_ROLE
//   2) GPIO strap at boot (optional)
//   3) NVS key "boot_role"
//   4) v_env.headless == true  → PUPPET
//   5) else SOLO
// =============================================================================

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_ROLE_SOLO   = 0,
    BOOT_ROLE_TYRANT = 1,  // display / orchestrator
    BOOT_ROLE_PUPPET = 2,  // headless logic worker
} boot_role_t;

// Resolved once in bootloader_final_app (or earlier).
boot_role_t boot_role_resolve(void);
const char* boot_role_name(boot_role_t r);

// Convenience
static inline bool boot_has_display(boot_role_t r) {
    return r == BOOT_ROLE_SOLO || r == BOOT_ROLE_TYRANT;
}
static inline bool boot_runs_apps_locally(boot_role_t r) {
    // Tyrant may still run a thin shell; full app logic is on puppets.
    // For v1: tyrant does NOT open WatchApp/games — only applies DOM.
    return r == BOOT_ROLE_SOLO || r == BOOT_ROLE_PUPPET;
}
static inline bool boot_sends_dom(boot_role_t r) {
    return r == BOOT_ROLE_PUPPET;
}
static inline bool boot_receives_dom(boot_role_t r) {
    return r == BOOT_ROLE_TYRANT;
}

#ifdef __cplusplus
}
#endif
