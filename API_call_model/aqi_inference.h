#ifndef AQI_INFERENCE_H
#define AQI_INFERENCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo hệ thống AI
int aqi_ai_init(void);

// Hàm dự đoán AQI
// Input: mảng 5 giá trị cảm biến mới nhất [PM2.5, PM10, CO, NO2, O3]
// Output: Giá trị AQI dự đoán
float aqi_ai_predict(float* new_sensor_data);

#ifdef __cplusplus
}
#endif

#endif