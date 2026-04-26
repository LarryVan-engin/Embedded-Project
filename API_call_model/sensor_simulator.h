#ifndef SENSOR_SIMULATOR_H
#define SENSOR_SIMULATOR_H

#ifdef __cplusplus
extern "C" {
#endif

// Cấu trúc gói dữ liệu cảm biến
typedef struct {
    float tvoc;
    float eco2;
    float iaq_actual; // Ground Truth từ thuật toán chuẩn UBA
} SensorData_t;

// Khởi tạo/Reset bộ mô phỏng
void SensorSim_Init(void);

// Đọc giá trị mô phỏng (Tạo ra sóng Sine quét qua các dải ô nhiễm)
SensorData_t SensorSim_Read(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_SIMULATOR_H