#include "drv_rwp.h"

void RWP_Unlock_Clock_MSTP(void)
{
    /* Write key 0xA5 to upper byte; set PRC0 (bit0=clock) + PRC1 (bit1=MSTP) */
    PRCR = (uint16_t)((0xA5U << 8U) | 0x03U);
}

void RWP_Lock_Clock_MSTP(void)
{
    /* Write key 0xA5; clear PRC0 + PRC1 (lower byte = 0x00 → locked) */
    PRCR = (uint16_t)(0xA5U << 8U);
}
