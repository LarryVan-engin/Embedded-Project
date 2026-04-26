#include "sensor_simulator.h"
#include <math.h>
#include <stdlib.h>

static float t_time = 0.0f;

void SensorSim_Init(void) {
    t_time = 0.0f;
}

// Hàm tính toán điểm UBA gốc để đối chiếu với AI
static float calculate_actual_iaq(float ppb) {
    float mg_m3 = (ppb / 1000.0f) / 0.5f;
    if (mg_m3 < 0.3f) return 1.0f + (mg_m3 / 0.3f) * 0.9f;
    else if (mg_m3 < 1.0f) return 2.0f + ((mg_m3 - 0.3f) / 0.7f) * 0.9f;
    else if (mg_m3 < 3.0f) return 3.0f + ((mg_m3 - 1.0f) / 2.0f) * 0.9f;
    else if (mg_m3 < 10.0f) return 4.0f + ((mg_m3 - 3.0f) / 7.0f) * 0.9f;
    return 5.0f;
}

SensorData_t SensorSim_Read(void) {
    SensorData_t data;
    
    // 1. Sinh sóng Sine cho TVOC: Dao động từ ~100 đến ~5500
    // Thêm một chút nhiễu ngẫu nhiên (noise) để dữ liệu tự nhiên hơn
    float noise = ((float)rand() / (float)RAND_MAX) * 100.0f - 50.0f; 
    data.tvoc = 2800.0f + 2700.0f * sin(t_time) + noise;

    if (data.tvoc < 10.0f) data.tvoc = 10.0f;
    if (data.tvoc > 6000.0f) data.tvoc = 6000.0f;

    // 2. Nội suy eCO2 từ TVOC (Mô phỏng hàm của ZMOD4410)
    float eco2_noise = ((float)rand() / (float)RAND_MAX) * 20.0f - 10.0f;
    data.eco2 = 400.0f + (data.tvoc * 0.6f) + eco2_noise;
    if (data.eco2 < 400.0f) data.eco2 = 400.0f;
    if (data.eco2 > 5000.0f) data.eco2 = 5000.0f;

    // 3. Tính điểm IAQ thực tế
    data.iaq_actual = calculate_actual_iaq(data.tvoc);

    // Tăng thời gian cho chu kỳ đọc tiếp theo
    t_time += 0.1f;
    
    return data;
}