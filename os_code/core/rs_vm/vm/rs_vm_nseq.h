#pragma once
// Packed native sequence — trapdoor array action.
// One C for-loop over interned nids. No per-step interpreter, no
// per-step UART sprintf-of-source ("translate").
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RSVM_NSEQ_MAGIC0  'N'
#define RSVM_NSEQ_MAGIC1  'S'
#define RSVM_NSEQ_MAGIC2  'Q'
#define RSVM_NSEQ_MAGIC3  '1'
#define RSVM_NSEQ_STRIDE  7
#define RSVM_NSEQ_MAX     64
#define RSVM_NSEQ_MAXARG  5

typedef enum {
    RSVM_NID_NOP        = 0,
    RSVM_NID_WAVE       = 1,
    RSVM_NID_WAVE_STOP  = 2,
    RSVM_NID_WAVE_FREQ  = 3,
    RSVM_NID_WAVE_DUTY  = 4,
    RSVM_NID_WAVE_AMP   = 5,
    RSVM_NID_SWEEP      = 6,
    RSVM_NID_ADC        = 7,
    RSVM_NID_SCOPE      = 8,
    RSVM_NID_DELAY      = 9,
    RSVM_NID_GPIO_WR    = 10,
    RSVM_NID_GPIO_RD    = 11,
    RSVM_NID_PIN_MODE   = 12,
    RSVM_NID_DIG_WR     = 13,
    RSVM_NID_DIG_RD     = 14,
    RSVM_NID_CORZ       = 15,
    RSVM_NID_COUNT
} rsvm_nid_t;

#pragma pack(push, 1)
typedef struct {
    int32_t nid;
    int32_t nargs;
    int32_t args[RSVM_NSEQ_MAXARG];
} rsvm_nstep_t;
#pragma pack(pop)

typedef int (*rsvm_nseq_host_fn)(int nid, const int32_t* args, int nargs,
                                 int32_t* out, void* user);

int         rsvm_nid_from_name(const char* name);
const char* rsvm_nid_name(int nid);

int rsvm_nseq_run(const rsvm_nstep_t* steps, int nsteps,
                  rsvm_nseq_host_fn host, void* user, int32_t* last_out);

/* UART / disk blob: NSQ1 | nsteps | flags | u16 reserved | steps[n] */
size_t rsvm_nseq_pack(const rsvm_nstep_t* steps, int nsteps,
                      uint8_t* out, size_t cap);
int    rsvm_nseq_unpack(const uint8_t* blob, size_t len,
                        rsvm_nstep_t* steps, int max_steps);

void rsvm_nseq_from_i32(const int32_t* flat, int nflat,
                        rsvm_nstep_t* steps, int* nsteps_io);

#ifdef __cplusplus
}
#endif
