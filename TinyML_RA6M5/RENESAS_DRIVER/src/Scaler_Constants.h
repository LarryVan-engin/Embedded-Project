#ifndef SCALER_CONSTANTS_H
#define SCALER_CONSTANTS_H

/**
 * @file  scaler_constants.h
 * @brief StandardScaler parameters for IAQ model input normalization.
 *
 * Generated from Python training pipeline (sklearn.preprocessing.StandardScaler).
 * Formula applied before each inference:
 *   tvoc_scaled = (tvoc_ppb - IAQ_MEAN[0]) / IAQ_SCALE[0]
 *
 * Training dataset statistics:
 *   Samples : ~50000 TVOC readings from synthetic ZMOD4410 sweep
 *   Range   : 10 - 6000 ppb
 *   MEAN    : 2812.12 ppb
 *   STD     : 1904.04 ppb
 */

/* Number of input features (TVOC only — single-feature model) */
#define SCALER_INPUT_SIZE 1

/* Mean of training TVOC distribution (ppb) */
static const float IAQ_MEAN[SCALER_INPUT_SIZE] = {
    2812.118256623945f
};

/* Standard deviation of training TVOC distribution (ppb) */
static const float IAQ_SCALE[SCALER_INPUT_SIZE] = {
    1904.0350619753567f
};

#endif /* SCALER_CONSTANTS_H */
