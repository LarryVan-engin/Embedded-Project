#ifndef DRV_I2C_H
#define DRV_I2C_H
#include <stdint.h>

/*
 * drv_i2c.h — RIIC (I2C) driver header for RA6M5.
 *
 * Replaces: IIC.h
 *
 * Register offsets per RA6M5 HW Manual §38:
 *   ICCR1=0x00, ICCR2=0x01, ICMR1=0x02, ICMR2=0x03, ICMR3=0x04
 *   ICFER=0x05, ICSER=0x06, ICIER=0x07, ICSR1=0x08, ICSR2=0x09
 *   ICBRL=0x10, ICBRH=0x11, ICDRT=0x12, ICDRR=0x13
 *
 * ICBRL/ICBRH: bits[7:5] must be written as 1 (RA6M5 manual §38.2.11).
 *   Write as: ICBRL(n) = ICBR_FIXED_BITS | count_value
 *
 * Correct RIIC init sequence (§38.3):
 *   1. ICCR1: ICE=0, IICRST=0  (disable)
 *   2. ICCR1: IICRST=1          (assert reset while disabled)
 *   3. ICCR1: ICE=1             (enable while in reset)
 *   4. Configure ICBRL, ICBRH, ICMR registers
 *   5. ICCR1: IICRST=0          (release reset — peripheral starts)
 */

/* -----------------------------------------------------------------------
 * RIIC base address and register accessors
 * ----------------------------------------------------------------------- */
#define RIIC_BASE    0x4009F000UL
#define RIIC_STRIDE  0x0100UL
#define RIIC_ADDR(n) (RIIC_BASE + RIIC_STRIDE * (uint32_t)(n))

#define ICCR1(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x00U))  /* I2C Bus Control 1          */
#define ICCR2(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x01U))  /* I2C Bus Control 2          */
#define ICMR1(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x02U))  /* I2C Bus Mode Register 1    */
#define ICMR2(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x03U))  /* I2C Bus Mode Register 2    */
#define ICMR3(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x04U))  /* I2C Bus Mode Register 3    */
#define ICFER(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x05U))  /* I2C Bus Function Enable    */
#define ICSER(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x06U))  /* I2C Bus Status Enable      */
#define ICIER(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x07U))  /* I2C Bus Interrupt Enable   */
#define ICSR1(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x08U))  /* I2C Bus Status Register 1  */
#define ICSR2(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x09U))  /* I2C Bus Status Register 2  */
#define ICBRL(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x10U))  /* I2C Bus Bit Rate Low       */
#define ICBRH(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x11U))  /* I2C Bus Bit Rate High      */
#define ICDRT(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x12U))  /* I2C Bus Transmit Data      */
#define ICDRR(n)  (*(volatile uint8_t *)(RIIC_ADDR(n) + 0x13U))  /* I2C Bus Receive Data       */

/* -----------------------------------------------------------------------
 * ICCR1 bits
 * ----------------------------------------------------------------------- */
#define ICCR1_ICE    (1U << 7)   /* I2C Bus Interface Enable                */
#define ICCR1_IICRST (1U << 6)   /* I2C Bus Interface Internal Reset        */

/* -----------------------------------------------------------------------
 * ICCR2 bits
 * ----------------------------------------------------------------------- */
#define ICCR2_BBSY (1U << 7)   /* Bus Busy                                  */
#define ICCR2_SP   (1U << 3)   /* Stop Condition Issuance Request           */
#define ICCR2_RS   (1U << 2)   /* Restart Condition Issuance Request        */
#define ICCR2_ST   (1U << 1)   /* Start Condition Issuance Request          */

/* -----------------------------------------------------------------------
 * ICSR2 bits
 * ----------------------------------------------------------------------- */
#define ICSR2_TDRE  (1U << 7)   /* Transmit Data Empty                      */
#define ICSR2_TEND  (1U << 6)   /* Transmit End                             */
#define ICSR2_RDRF  (1U << 5)   /* Receive Data Full                        */
#define ICSR2_NACKF (1U << 4)   /* NACK Detection Flag                      */
#define ICSR2_STOP  (1U << 3)   /* Stop Condition Detection Flag            */
#define ICSR2_START (1U << 2)   /* Start Condition Detection Flag           */

/* -----------------------------------------------------------------------
 * ICMR3 bits
 * ----------------------------------------------------------------------- */
#define ICMR3_ACKWP (1U << 4)   /* ACKBT Write Protect (set before ACKBT)  */
#define ICMR3_ACKBT (1U << 3)   /* Transmission ACK/NACK: 0=ACK, 1=NACK    */

/* -----------------------------------------------------------------------
 * ICBRL/ICBRH: upper 3 bits must always be written as 1
 * ----------------------------------------------------------------------- */
#define ICBR_FIXED_BITS  0xE0U

/* -----------------------------------------------------------------------
 * I2C channel enum
 * ----------------------------------------------------------------------- */
typedef enum {
    I2C0 = 0,
    I2C1 = 1,
    I2C2 = 2
} I2C_t;

/* -----------------------------------------------------------------------
 * I2C bus speed
 * ----------------------------------------------------------------------- */
typedef enum {
    I2C_SPEED_STANDARD = 100000,   /* 100 kHz standard mode */
    I2C_SPEED_FAST     = 400000    /* 400 kHz fast mode     */
} I2C_SPEED_t;

/* -----------------------------------------------------------------------
 * Transfer direction (R/W bit in address byte)
 * ----------------------------------------------------------------------- */
typedef enum {
    I2C_WRITE = 0,   /* Master writes to slave (R/W bit = 0) */
    I2C_READ  = 1    /* Master reads from slave (R/W bit = 1) */
} I2C_DIR_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */
void    I2C_Init(I2C_t i2c, uint8_t pclkb_mhz, I2C_SPEED_t speed);
void    I2C_Start(I2C_t i2c);
void    I2C_Stop(I2C_t i2c);
uint8_t I2C_Transmit_Address(I2C_t i2c, uint8_t address, I2C_DIR_t dir);
uint8_t I2C_Master_Transmit_Data(I2C_t i2c, uint8_t *data, uint8_t length);
uint8_t I2C_Master_Receive_Data(I2C_t i2c, uint8_t *data, uint8_t length);

#endif /* DRV_I2C_H */
