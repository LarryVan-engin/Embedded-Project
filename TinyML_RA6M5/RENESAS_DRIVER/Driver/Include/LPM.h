/* Deprecated: merged into drv_clk.h (Phase 5 reorganization, S-01 fix) */
#include "drv_clk.h"

/* Backward-compatibility alias: LPM_Unlock -> CLK_ModuleStart_SCI */
#define LPM_Unlock(p)  CLK_ModuleStart_SCI(p)
