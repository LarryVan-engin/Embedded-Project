#include "drv_i2c.h"
#include "drv_clk.h"
#include "drv_rwp.h"
#include "drv_common.h"
#include "GPIO.h"

/* -----------------------------------------------------------------------
 * Forward declarations for internal helpers
 * ----------------------------------------------------------------------- */
static void i2c_clock_init(I2C_t i2c);
static void i2c_pin_config(I2C_t i2c);
static void i2c_bit_delay(void);
static void i2c_bus_recover(I2C_t i2c);

/* -----------------------------------------------------------------------
 * i2c_clock_init — release module stop for one RIIC channel.
 *
 * MSTPCRB bit mapping (RA6M5 HW manual §9.3.3):
 *   I2C0 → bit 9,  I2C1 → bit 8,  I2C2 → bit 7
 * ----------------------------------------------------------------------- */
static void i2c_clock_init(I2C_t i2c)
{
    RWP_Unlock_Clock_MSTP();

    switch (i2c)
    {
        case I2C0: MSTPCRB &= ~(1UL << 9U); break;
        case I2C1: MSTPCRB &= ~(1UL << 8U); break;
        case I2C2: MSTPCRB &= ~(1UL << 7U); break;
        default:   break;
    }

    RWP_Lock_Clock_MSTP();
}

/* -----------------------------------------------------------------------
 * i2c_pin_config — configure SCL/SDA pins for the selected RIIC channel.
 *
 * Pin mapping (EK-RA6M5 schematic, PSEL=0x07 for RIIC function):
 *   I2C0: SCL=P400, SDA=P401
 *   I2C1: SCL=P512, SDA=P511
 *   I2C2: SCL=P410, SDA=P409
 *
 * I2C pins require NCODR=1 (open-drain) and PCR=1 (internal pull-up ~50 kΩ).
 * The internal pull-up is sufficient for short PCB traces (< 10 cm).
 * For long wires or fast speeds (400 kHz) add external 4.7 kΩ resistors.
 * ----------------------------------------------------------------------- */
static void i2c_pin_config(I2C_t i2c)
{
    uint8_t  scl_port, scl_pin;
    uint8_t  sda_port, sda_pin;
    const uint32_t psel = 0x07U;

    switch (i2c)
    {
        case I2C0: scl_port=4U; scl_pin=0U;  sda_port=4U; sda_pin=1U;  break;
        case I2C1: scl_port=5U; scl_pin=12U; sda_port=5U; sda_pin=11U; break;
        case I2C2: scl_port=4U; scl_pin=10U; sda_port=4U; sda_pin=9U;  break;
        default: return;
    }

    PWPR = 0x00U;
    PWPR = 0x40U;

    PmnPFS(scl_port, scl_pin)  = PmnPFS_PSEL(psel);
    PmnPFS(scl_port, scl_pin) |= PmnPFS_PMR;
    PmnPFS(scl_port, scl_pin) |= PmnPFS_NCODR;
    PmnPFS(scl_port, scl_pin) |= PmnPFS_PCR;    /* enable internal pull-up */

    PmnPFS(sda_port, sda_pin)  = PmnPFS_PSEL(psel);
    PmnPFS(sda_port, sda_pin) |= PmnPFS_PMR;
    PmnPFS(sda_port, sda_pin) |= PmnPFS_NCODR;
    PmnPFS(sda_port, sda_pin) |= PmnPFS_PCR;    /* enable internal pull-up */

    PWPR = 0x00U;
    PWPR = 0x80U;
}

/* -----------------------------------------------------------------------
 * i2c_bit_delay — ~10 μs delay for bit-bang recovery sequence.
 * 2000 NOPs at 200 MHz ICLK ≈ 10 μs.
 * ----------------------------------------------------------------------- */
static void i2c_bit_delay(void)
{
    volatile uint32_t d = 2000U;
    while (d-- != 0U)
    {
        __asm volatile ("nop");
    }
}

/* -----------------------------------------------------------------------
 * i2c_bus_recover — 9-clock SCL recovery for SDA stuck-low (S-06).
 *
 * Implements the standard I2C bus recovery procedure (MIPI I2C spec §3.1.16):
 *   1. Disable RIIC and switch SCL/SDA to GPIO bit-bang mode.
 *   2. Drive SCL high; toggle SCL 9 times.
 *   3. After each toggle, check if SDA has been released by the slave.
 *   4. Issue a manual STOP condition (SDA low→high while SCL high).
 *   5. Reconfigure SCL/SDA as RIIC peripheral function.
 *   6. Perform a soft IICRST reset to flush RIIC state machine.
 *
 * Called from I2C_Start when BBSY remains set after DRV_TIMEOUT_TICKS.
 * If SCL is stuck low (hardware failure), recovery will not help —
 * the caller will still see timeout on the next attempt.
 * ----------------------------------------------------------------------- */
static void i2c_bus_recover(I2C_t i2c)
{
    uint8_t n = (uint8_t)i2c;
    uint8_t scl_port, scl_pin, sda_port, sda_pin;
    uint8_t k;

    switch (i2c)
    {
        case I2C0: scl_port=4U; scl_pin=0U;  sda_port=4U; sda_pin=1U;  break;
        case I2C1: scl_port=5U; scl_pin=12U; sda_port=5U; sda_pin=11U; break;
        case I2C2: scl_port=4U; scl_pin=10U; sda_port=4U; sda_pin=9U;  break;
        default: return;
    }

    /* GPIO_PORT_t enum values are ASCII digits/letters ('0'–'9','A','B').
     * Ports 4 and 5 (only used here) map directly: '0'+4='4', '0'+5='5'. */
    GPIO_PORT_t scl_gpio = (GPIO_PORT_t)((uint8_t)'0' + scl_port);
    GPIO_PORT_t sda_gpio = (GPIO_PORT_t)((uint8_t)'0' + sda_port);

    /* Step 1: disable RIIC */
    ICCR1(n) = 0x00U;

    /* Step 2: switch SCL to GPIO open-drain output, SDA to GPIO input */
    GPIO_Config(scl_gpio, scl_pin, GPIO_CNF_OUT_OD, GPIO_MODE_OUTPUT);
    GPIO_Config(sda_gpio, sda_pin, GPIO_CNF_IN_FLT,  GPIO_MODE_INPUT);

    /* Step 3: drive SCL high */
    GPIO_Write_Pin(scl_gpio, scl_pin, GPIO_PIN_SET);
    i2c_bit_delay();

    /* Step 4: toggle SCL 9 times; stop early if SDA is released */
    for (k = 0U; k < 9U; k++)
    {
        GPIO_Write_Pin(scl_gpio, scl_pin, GPIO_PIN_RESET);
        i2c_bit_delay();
        GPIO_Write_Pin(scl_gpio, scl_pin, GPIO_PIN_SET);
        i2c_bit_delay();

        if (GPIO_Read_Pin(sda_gpio, sda_pin) != 0U)
        {
            break;   /* SDA released — slave let go */
        }
    }

    /* Step 5: issue manual STOP (SDA low → high while SCL is high) */
    GPIO_Config(sda_gpio, sda_pin, GPIO_CNF_OUT_OD, GPIO_MODE_OUTPUT);
    GPIO_Write_Pin(sda_gpio, sda_pin, GPIO_PIN_RESET);  /* SDA low  */
    i2c_bit_delay();
    GPIO_Write_Pin(scl_gpio, scl_pin, GPIO_PIN_SET);    /* SCL high */
    i2c_bit_delay();
    GPIO_Write_Pin(sda_gpio, sda_pin, GPIO_PIN_SET);    /* SDA high → STOP */
    i2c_bit_delay();

    /* Step 6: reconfigure pins as RIIC peripheral */
    i2c_pin_config(i2c);

    /* Step 7: soft reset RIIC state machine */
    ICCR1(n) |= ICCR1_IICRST;
    ICCR1(n) |= ICCR1_ICE;
    ICCR1(n) &= (uint8_t)~ICCR1_IICRST;
}

/* -----------------------------------------------------------------------
 * I2C_Init — initialise RIIC channel in master mode.
 *
 * Correct init sequence (RA6M5 HW manual §38.3):
 *   1. ICCR1 = 0x00          (ICE=0, IICRST=0  — disable first)
 *   2. ICCR1 |= IICRST        (assert reset while disabled)
 *   3. ICCR1 |= ICE           (enable while in reset)
 *   4. Configure ICBRL, ICBRH, ICMR registers
 *   5. ICCR1 &= ~IICRST       (release reset — peripheral starts)
 *
 * BRR: total_counts = PCLKB / speed_hz - 2; ICBRL = ICBRH = 0xE0 | (total/2)
 * ----------------------------------------------------------------------- */
void I2C_Init(I2C_t i2c, uint8_t pclkb_mhz, I2C_SPEED_t speed)
{
    uint8_t n = (uint8_t)i2c;
    if (n > 2U) { return; }

    i2c_clock_init(i2c);
    i2c_pin_config(i2c);

    uint32_t pclkb_hz = (uint32_t)pclkb_mhz * 1000000UL;
    uint8_t  cks = 0U;
    uint32_t br;

    /* Calculate optimal CKS divider and BR count */
    for (cks = 0U; cks < 8U; cks++)
    {
        uint32_t ref_clk = pclkb_hz / (1UL << cks);
        uint32_t total   = (ref_clk / (uint32_t)speed);
        
        if (total >= 2U) {
            br = (total - 2U) / 2U;
            if (br <= 31U) {
                break; /* Found a divider that fits in 5-bit ICBR */
            }
        }
    }

    if (cks >= 8U) {
        cks = 7U;
        br = 31U; /* Fallback to slowest possible */
    }

    ICCR1(n) = 0x00U;
    ICCR1(n) |= ICCR1_IICRST;
    ICCR1(n) |= ICCR1_ICE;

    ICMR1(n) = (uint8_t)(cks << 4U);  /* Set CKS[2:0] at bits 6:4 */
    ICBRL(n) = (uint8_t)(ICBR_FIXED_BITS | br);
    ICBRH(n) = (uint8_t)(ICBR_FIXED_BITS | br);

    ICMR2(n) = 0x00U;
    ICMR3(n) = 0x00U;

    ICFER(n) &= (uint8_t)~(1U << 0U);

    ICCR1(n) &= (uint8_t)~ICCR1_IICRST;
}

/* -----------------------------------------------------------------------
 * I2C_Start — generate a START condition on the I2C bus.
 *
 * S-05: both busy-wait loops are now timeout-protected.
 * S-06: if BBSY is stuck on first wait, bus recovery is attempted once.
 * ----------------------------------------------------------------------- */
void I2C_Start(I2C_t i2c)
{
    uint8_t  n  = (uint8_t)i2c;
    uint32_t to = DRV_TIMEOUT_TICKS;
    if (n > 2U) { return; }

    while (ICCR2(n) & ICCR2_BBSY)
    {
        if (--to == 0U)
        {
            i2c_bus_recover(i2c);   /* S-06: attempt 9-clock SCL recovery */
            break;                  /* Proceed to generate START */
        }
    }

    /* Flush any leftover received bytes to ensure clean state */
    if (ICSR2(n) & ICSR2_RDRF)
    {
        volatile uint8_t flush = ICDRR(n);
        (void)flush;
    }

    ICCR2(n) |= ICCR2_ST;

    to = DRV_TIMEOUT_TICKS;
    while (!(ICCR2(n) & ICCR2_BBSY))
    {
        if (--to == 0U) { return; }
    }
}

/* -----------------------------------------------------------------------
 * I2C_Stop — generate a STOP condition on the I2C bus.
 * S-05: both busy-wait loops are timeout-protected.
 * ----------------------------------------------------------------------- */
void I2C_Stop(I2C_t i2c)
{
    uint8_t  n  = (uint8_t)i2c;
    uint32_t to = DRV_TIMEOUT_TICKS;
    if (n > 2U) { return; }

    ICCR2(n) |= ICCR2_SP;

    while (!(ICSR2(n) & ICSR2_STOP))
    {
        if (--to == 0U) { return; }
    }
    ICSR2(n) &= (uint8_t)~ICSR2_STOP;

    to = DRV_TIMEOUT_TICKS;
    while (ICCR2(n) & ICCR2_BBSY)
    {
        if (--to == 0U) { return; }
    }
}

/* -----------------------------------------------------------------------
 * I2C_Transmit_Address — send 7-bit slave address + R/W bit.
 * Returns 1 on ACK, 0 on NACK or timeout.
 * S-05: TDRE and TEND waits are timeout-protected.
 * ----------------------------------------------------------------------- */
uint8_t I2C_Transmit_Address(I2C_t i2c, uint8_t address, I2C_DIR_t dir)
{
    uint8_t  n  = (uint8_t)i2c;
    uint32_t to = DRV_TIMEOUT_TICKS;
    if (n > 2U) { return 0U; }

    while (!(ICSR2(n) & ICSR2_TDRE))
    {
        if (--to == 0U) { return 0U; }
    }

    ICDRT(n) = (uint8_t)((address << 1U) | ((uint8_t)dir & 0x01U));

    to = DRV_TIMEOUT_TICKS;
    if (dir == I2C_WRITE)
    {
        while (!(ICSR2(n) & ICSR2_TEND))
        {
            if (--to == 0U) { return 0U; }
        }
    }
    else
    {
        /* For READ address, wait for RDRF (ACK) or NACKF (NACK) */
        while (!(ICSR2(n) & (ICSR2_RDRF | ICSR2_NACKF)))
        {
            if (--to == 0U) { return 0U; }
        }
    }

    if (ICSR2(n) & ICSR2_NACKF)
    {
        ICSR2(n) &= (uint8_t)~ICSR2_NACKF;
        return 0U;
    }

    return 1U;
}

/* -----------------------------------------------------------------------
 * I2C_Master_Transmit_Data — send data bytes to slave.
 * Returns 1 on success, 0 on NACK or timeout.
 * S-05: TDRE and TEND waits are timeout-protected.
 * ----------------------------------------------------------------------- */
uint8_t I2C_Master_Transmit_Data(I2C_t i2c, uint8_t *data, uint8_t length)
{
    uint8_t  n  = (uint8_t)i2c;
    uint8_t  i;
    uint32_t to;
    if (n > 2U) { return 0U; }

    for (i = 0U; i < length; i++)
    {
        to = DRV_TIMEOUT_TICKS;
        while (!(ICSR2(n) & ICSR2_TDRE))
        {
            if (--to == 0U) { I2C_Stop(i2c); return 0U; }
        }

        ICDRT(n) = data[i];

        to = DRV_TIMEOUT_TICKS;
        while (!(ICSR2(n) & ICSR2_TEND))
        {
            if (--to == 0U) { I2C_Stop(i2c); return 0U; }
        }

        if (ICSR2(n) & ICSR2_NACKF)
        {
            ICSR2(n) &= (uint8_t)~ICSR2_NACKF;
            I2C_Stop(i2c);
            return 0U;
        }
    }

    return 1U;
}

/* -----------------------------------------------------------------------
 * I2C_Master_Receive_Data — receive data bytes from slave.
 *
 * ACK/NACK sequencing (RA6M5 §38.3 master receive):
 *   ACKWP must be set BEFORE writing ACKBT (was reversed in original).
 *   Last byte: NACK (ACKBT=1).  All others: ACK (ACKBT=0).
 * S-05: RDRF wait is timeout-protected.
 * Returns 1 on success, 0 on timeout.
 * ----------------------------------------------------------------------- */
uint8_t I2C_Master_Receive_Data(I2C_t i2c, uint8_t *data, uint8_t length)
{
    uint8_t  n  = (uint8_t)i2c;
    uint8_t  i;
    uint32_t to;
    if (n > 2U) { return 0U; }
    if (length == 0U) { return 1U; }

    /* DUMMY READ: wait for RDRF from address transmission, then read to start clocking */
    to = DRV_TIMEOUT_TICKS;
    while (!(ICSR2(n) & ICSR2_RDRF))
    {
        if (--to == 0U) { I2C_Stop(i2c); return 0U; }
    }

    /* Set ACKBT for the FIRST real data byte */
    ICMR3(n) |= ICMR3_ACKWP;
    if (length == 1U) { ICMR3(n) |= ICMR3_ACKBT; }
    else              { ICMR3(n) &= (uint8_t)~ICMR3_ACKBT; }
    ICMR3(n) &= (uint8_t)~ICMR3_ACKWP;

    /* Discard address byte, release SCL */
    volatile uint8_t dummy = ICDRR(n);
    (void)dummy;

    for (i = 0U; i < length; i++)
    {
        to = DRV_TIMEOUT_TICKS;
        while (!(ICSR2(n) & ICSR2_RDRF))
        {
            if (--to == 0U) { I2C_Stop(i2c); return 0U; }
        }

        /* Set ACKBT for the NEXT byte or STOP for the LAST byte */
        if (i < (uint8_t)(length - 1U))
        {
            ICMR3(n) |= ICMR3_ACKWP;
            if (i == (uint8_t)(length - 2U)) { ICMR3(n) |= ICMR3_ACKBT; }
            else                             { ICMR3(n) &= (uint8_t)~ICMR3_ACKBT; }
            ICMR3(n) &= (uint8_t)~ICMR3_ACKWP;
        }
        else
        {
            /* BEFORE reading the last byte, we MUST set SP=1 to generate a STOP condition! */
            ICCR2(n) |= ICCR2_SP;
        }

        data[i] = ICDRR(n);
    }

    /* Wait for STOP condition to finish */
    to = DRV_TIMEOUT_TICKS;
    while (!(ICSR2(n) & ICSR2_STOP))
    {
        if (--to == 0U) { return 0U; }
    }
    ICSR2(n) &= (uint8_t)~ICSR2_STOP;

    /* Wait for bus to be freed */
    to = DRV_TIMEOUT_TICKS;
    while (ICCR2(n) & ICCR2_BBSY)
    {
        if (--to == 0U) { return 0U; }
    }

    return 1U;
}
