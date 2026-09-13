/* Standalone pack/run test: g++ -I. test_nseq.c rs_vm_nseq.cpp -o test_nseq */
#include "rs_vm_nseq.h"
#include <stdio.h>

static int mock(int nid, const int32_t* a, int n, int32_t* out, void* user) {
    int* count = (int*)user;
    (*count)++;
    if (out) *out = nid * 10 + (n > 0 && a ? a[0] : 0);
    return 0;
}

int main(void) {
    rsvm_nstep_t s[3] = {
        {RSVM_NID_WAVE, 5, {1, 1000, 50, 4, 80}},
        {RSVM_NID_DELAY, 1, {10, 0, 0, 0, 0}},
        {RSVM_NID_WAVE_STOP, 0, {0, 0, 0, 0, 0}},
    };
    int count = 0;
    int32_t last = 0;
    rsvm_nseq_run(s, 3, mock, &count, &last);
    if (count != 3) { fprintf(stderr, "count %d\n", count); return 1; }
    if (last != RSVM_NID_WAVE_STOP * 10) { fprintf(stderr, "last %d\n", (int)last); return 1; }

    uint8_t blob[512];
    size_t n = rsvm_nseq_pack(s, 3, blob, sizeof blob);
    if (n < 8) return 2;
    rsvm_nstep_t out[8];
    int got = rsvm_nseq_unpack(blob, n, out, 8);
    if (got != 3 || out[0].nid != RSVM_NID_WAVE || out[0].args[1] != 1000) return 3;
    if (rsvm_nid_from_name("wave") != RSVM_NID_WAVE) return 4;
    if (rsvm_nid_from_name("delay") != RSVM_NID_DELAY) return 5;

    int32_t flat[] = {1, 5, 1, 1000, 50, 4, 80,  2, 0, 0, 0, 0, 0, 0};
    rsvm_nstep_t st[4];
    int ns = 4;
    rsvm_nseq_from_i32(flat, 14, st, &ns);
    if (ns != 2 || st[1].nid != 2) return 6;
    puts("nseq ok");
    return 0;
}
