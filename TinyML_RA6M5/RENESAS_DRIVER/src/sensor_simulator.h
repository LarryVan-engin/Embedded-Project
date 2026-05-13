#ifndef SENSOR_SIMULATOR_H
#define SENSOR_SIMULATOR_H

/**
 * @file  sensor_simulator.h
 * @brief Synthetic TVOC/eCO2 data source for offline IAQ inference testing.
 *
 * Simulates the ZMOD4410 gas sensor output without real hardware.
 * The simulator sweeps TVOC through a sine-wave trajectory (100 - 5500 ppb)
 * with small Gaussian noise, then derives a ground-truth IAQ rating using
 * the UBA reference formula. This allows direct comparison of the AI forecast
 * against the analytical baseline.
 *
 * Usage:
 *   SensorSim_Init();
 *   while (running) {
 *       SensorPacket_t pkt = SensorSim_Read();
 *       float forecast = IAQ_Predict(pkt.tvoc);
 *       // compare forecast vs pkt.iaq_reference
 *   }
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One sample from the simulated sensor.
 */
typedef struct {
    float tvoc;           /**< Total VOC concentration (ppb)           */
    float eco2;           /**< Estimated CO2 equivalent (ppm)          */
    float iaq_reference;  /**< Analytical IAQ via UBA formula (1-5)    */
    float temperature;    /**< Simulated Temperature (Celsius)         */
    float humidity;       /**< Simulated Relative Humidity (%)         */
} SensorPacket_t;

/**
 * @brief Reset the simulator time counter. Call once before the test loop.
 */
void SensorSim_Init(void);

/**
 * @brief Generate the next simulated sensor reading.
 *        Advances internal time by one step (~12-minute equivalent interval).
 * @return SensorPacket_t with TVOC, eCO2, and reference IAQ.
 */
SensorPacket_t SensorSim_Read(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SIMULATOR_H */
