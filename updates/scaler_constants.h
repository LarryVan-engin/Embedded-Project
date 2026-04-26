#ifndef SCALER_CONSTANTS_H
#define SCALER_CONSTANTS_H

// Số lượng đầu vào
#define SCALER_INPUT_SIZE 1

// Bộ hằng số MEAN từ StandardScaler
const float IAQ_MEAN[SCALER_INPUT_SIZE] = {
    2812.118256623945
};

// Bộ hằng số SCALE (Standard Deviation) từ StandardScaler
const float IAQ_SCALE[SCALER_INPUT_SIZE] = {
    1904.0350619753567
};

#endif // SCALER_CONSTANTS_H