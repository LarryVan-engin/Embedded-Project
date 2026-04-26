#include <iostream>
#include <stdio.h>  // Dành cho snprintf
#include <chrono>
#include <thread>
#include "iaq_predictor.h"
#include "sensor_simulator.h"

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  KHOI DONG SIL TEST (JSON PAYLOAD TO ESP32)      " << std::endl;
    std::cout << "==================================================\n" << std::endl;

    // 1. Khởi tạo AI
    if (!IAQ_AI_Init()) {
        std::cerr << "[LOI] Khoi tao AI TFLM that bai!" << std::endl;
        return -1;
    }
    
    // 2. Khởi tạo Cảm biến ảo
    SensorSim_Init();
    std::cout << "-> He thong san sang. Bat dau mo phong gui du lieu...\n" << std::endl;

    // 3. Vòng lặp chính (Mô phỏng vòng lặp while(1) trên MCU)
    for (int i = 1; i <= 30; i++) {
        
        // Bước A: "Đọc" cảm biến ZMOD4410
        SensorData_t sensor = SensorSim_Read();

        // Bước B: Chạy Edge AI trên RA6M5 để dự đoán
        float iaq_forecast = IAQ_AI_Predict(sensor.tvoc);

        // Bước C: Đóng gói thành chuỗi JSON chuẩn MQTT
        // Lưu ý: Chuỗi này phải khớp hoàn toàn với những gì mqtt_client.py trên Server đang chờ
        char uart_payload[256];
        snprintf(uart_payload, sizeof(uart_payload), 
                 "{\"tvoc\":%.1f, \"eco2\":%.1f, \"iaq_actual\":%.2f, \"iaq_forecast\":%.2f}", 
                 sensor.tvoc, sensor.eco2, sensor.iaq_actual, iaq_forecast);

        // Bước D: Mô phỏng truyền UART sang ESP32
        // Trên MCU thực tế, bạn sẽ dùng lệnh: R_SCI_UART_Write(&g_uart_ctrl, (uint8_t*)uart_payload, strlen(uart_payload));
        std::cout << "[UART TX -> ESP32] " << uart_payload << std::endl;

        // Tạm dừng 0.5s để dễ quan sát trên màn hình máy tính (Trên MCU thực tế là R_BSP_SoftwareDelay)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n==================================================" << std::endl;
    std::cout << "Hoan thanh chuoi test. Code C++ san sang de nạp MCU." << std::endl;
    return 0;
}