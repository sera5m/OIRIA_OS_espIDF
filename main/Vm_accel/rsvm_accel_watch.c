/* Watch trapdoor body: stay in Q15 LUT, no libm, no JIT. */
#include "rsvm_accel.h"

#if defined(RSVM_TARGET_WATCH)
int rsvm_accel_watch_ready(void) { return 1; }
#else
int rsvm_accel_watch_ready(void) { return 0; }
#endif
