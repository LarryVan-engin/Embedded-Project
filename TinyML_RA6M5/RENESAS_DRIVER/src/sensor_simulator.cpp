/**
 * @file  sensor_simulator.cpp
 * @brief Synthetic sensor data generator for IAQ forecasting tests.
 */

#include "sensor_simulator.h"
#include <math.h>
#include <stdint.h>

/* Internal time counter */
static float s_time = 0.0f;

/* Custom LCG state for deterministic, lock-free random number generation */
static uint32_t s_prng_state = 123456789U;

/* =========================================================================
 * get_random_float
 * Generate pseudo-random float between 0.0 and 1.0 without stdlib rand()
 * ========================================================================= */
static float get_random_float(void)
{
    /* LCG parameters */
    s_prng_state = (1103515245U * s_prng_state + 12345U);
    /* Use upper 23 bits for float precision */
    uint32_t rand_val = (s_prng_state >> 9) & 0x007FFFFFU;
    return (float)rand_val / (float)0x007FFFFFU;
}

/* =========================================================================
 * SensorSim_Init
 * ========================================================================= */
void SensorSim_Init(void)
{
    s_time = 0.0f;
    s_prng_state = 123456789U; /* Reset seed */
}

/* =========================================================================
 * calculate_reference_iaq
 * ========================================================================= */
static float calculate_reference_iaq(float tvoc_ppb)
{
    float mg_m3 = (tvoc_ppb / 1000.0f) / 0.5f;

    if      (mg_m3 < 0.3f)  return 1.0f + (mg_m3 / 0.3f) * 0.9f;
    else if (mg_m3 < 1.0f)  return 2.0f + ((mg_m3 - 0.3f) / 0.7f) * 0.9f;
    else if (mg_m3 < 3.0f)  return 3.0f + ((mg_m3 - 1.0f) / 2.0f) * 0.9f;
    else if (mg_m3 < 10.0f) return 4.0f + ((mg_m3 - 3.0f) / 7.0f) * 0.9f;
    else                    return 5.0f;
}

/* =========================================================================
 * SensorSim_Read
 * ========================================================================= */
SensorPacket_t SensorSim_Read(void)
{
    SensorPacket_t pkt;

    /* --- TVOC: sine sweep with small noise (+/- 50.0f) --- */
    float noise = (get_random_float() * 100.0f) - 50.0f;
    pkt.tvoc = 2800.0f + 2700.0f * sinf(s_time) + noise;
    
    if (pkt.tvoc <   10.0f) pkt.tvoc =   10.0f;
    if (pkt.tvoc > 6000.0f) pkt.tvoc = 6000.0f;

    /* --- eCO2: cross-sensitivity model from TVOC (+/- 10.0f) --- */
    float eco2_noise = (get_random_float() * 20.0f) - 10.0f;
    pkt.eco2 = 400.0f + (pkt.tvoc * 0.6f) + eco2_noise;
    
    if (pkt.eco2 <  400.0f) pkt.eco2 =  400.0f;
    if (pkt.eco2 > 5000.0f) pkt.eco2 = 5000.0f;

    /* --- Reference IAQ --- */
    pkt.iaq_reference = calculate_reference_iaq(pkt.tvoc);

    /* --- Temperature and Humidity --- */
    pkt.temperature = 25.0f + (get_random_float() * 10.0f); /* 25.0 to 35.0 C */
    pkt.humidity    = 40.0f + (get_random_float() * 20.0f); /* 40.0 to 60.0 % */

    /* Advance time */
    s_time += 0.1f;

    return pkt;
}