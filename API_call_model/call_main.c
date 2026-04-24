#include "aqi_inference.h"

void ai_thread_entry(void) {
    // Khởi tạo AI một lần duy nhất
    if(aqi_ai_init() == 0) {
        // AI sẵn sàng
    }

    while(1) {
        // Giả sử đọc được dữ liệu mới từ ZMOD4410
        float current_sensors[5] = {20.5, 30.2, 400.0, 25.1, 55.0}; 

        // Gọi AI dự đoán cho 1 giờ tới
        float predicted_aqi = aqi_ai_predict(current_sensors);

        // Gửi kết quả predicted_aqi lên Dashboard qua WiFi
        tx_thread_sleep(3600 * TX_TIMER_TICKS_PER_SECOND); // Chờ 1 giờ
    }
}