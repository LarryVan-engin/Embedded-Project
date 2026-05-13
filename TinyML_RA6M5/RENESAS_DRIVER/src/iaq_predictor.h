#ifndef IAQ_PREDICTOR_H
#define IAQ_PREDICTOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  iaq_predictor.h
 * @brief TFLite Micro inference wrapper for IAQ future forecasting.
 *
 * Model purpose:
 *   Given a current TVOC reading (ppb), predict the FUTURE Indoor Air Quality
 *   rating (UBA scale 1.0 - 5.0) after the system reaches steady state.
 *
 *   This is NOT a real-time mapping of TVOC -> IAQ.
 *   The model was trained on time-series data to capture how IAQ *evolves*
 *   after a TVOC change, accounting for ventilation lag and sensor drift.
 *
 * UBA IAQ Scale:
 *   1.0 - 1.5  Excellent
 *   1.5 - 2.5  Good
 *   2.5 - 3.5  Moderate
 *   3.5 - 4.5  Poor
 *   4.5 - 5.0  Very Poor
 */

/**
 * @brief Initialize the TFLite Micro interpreter.
 *        Must be called once before any call to IAQ_Predict().
 * @return true on success, false on model schema mismatch or memory failure.
 */
bool IAQ_Init(void);

/**
 * @brief Run inference to forecast future IAQ from current TVOC.
 * @param tvoc_ppb  Current TVOC sensor reading in parts-per-billion.
 * @return Predicted future IAQ rating clamped to [1.0, 5.0].
 *         Returns -1.0f if IAQ_Init() was not called successfully.
 */
float IAQ_Predict(float tvoc_ppb);

#ifdef __cplusplus
}
#endif

#endif /* IAQ_PREDICTOR_H */
