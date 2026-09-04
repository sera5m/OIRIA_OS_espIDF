#pragma once
/* Compile targets for Vulcan accel / trapdoor. */

#if defined(RSVM_IN_FIRMWARE) || defined(ESP_PLATFORM)
#  ifndef RSVM_TARGET_WATCH
#    define RSVM_TARGET_WATCH 1
#  endif
#else
#  ifndef RSVM_TARGET_DESKTOP
#    define RSVM_TARGET_DESKTOP 1
#  endif
#endif

#if defined(RSVM_TARGET_WATCH)
#  define RSVM_ACCEL_STACK     64
#  define RSVM_ACCEL_SLOTS     48
#  define RSVM_ACCEL_LUT_BITS  8   /* 256-entry Q15 table */
#  define RSVM_ACCEL_HAS_JIT   0
#else
#  define RSVM_ACCEL_STACK     4096
#  define RSVM_ACCEL_SLOTS     256
#  define RSVM_ACCEL_LUT_BITS  12  /* 4096-entry desktop table (optional) */
#  define RSVM_ACCEL_HAS_JIT   1
#endif

#define RSVM_ACCEL_LUT_N  (1u << 8)  /* shared 256-pt Q15 always present */
