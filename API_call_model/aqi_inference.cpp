#include "aqi_inference.h"
#include "aqi_model_data.h"     // Chứa mảng g_aqi_model_data
#include "Scaler_Constants.h"   // Chứa mảng AQI_MEAN và AQI_SCALE
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Cấu hình AI
namespace {
    const tflite::Model* model = nullptr;
    tflite::MicroInterpreter* interpreter = nullptr;
    TfLiteTensor* input = nullptr;
    TfLiteTensor* output = nullptr;

    // Bộ nhớ đệm cho TFLite (Arena) - RA6M5 có 512KB SRAM nên 50KB là rất an toàn
    constexpr int kTensorArenaSize = 50 * 1024;
    alignas(16) uint8_t tensor_arena[kTensorArenaSize];

    // Circular Buffer để lưu 6 mốc thời gian gần nhất (6h * 5 features = 30 số)
    float sliding_window[30] = {0.0f};
}

// Khởi tạo AI
int aqi_ai_init(void) {
    tflite::InitializeTarget();

    // 1. Load model từ mảng Hex trong aqi_model_data.h
    model = tflite::GetModel(g_aqi_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        return -1; // Sai version model
    }

    // 2. Thiết lập OpsResolver (Chứa các toán tử của MLP như FullyConnected, Relu)
    static tflite::AllOpsResolver resolver;

    // 3. Khởi tạo Interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // 4. Cấp phát tensors
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        return -2;
    }

    // 5. Trỏ tới input và output tensors
    input = interpreter->input(0);
    output = interpreter->output(0);

    return 0; // Thành công
}

// Hàm dự đoán
float aqi_ai_predict(float eco2_raw, float tvoc_raw) {
    // 1. Tạo mảng 5 đặc trưng "giả lập" từ 2 chỉ số thực
    // Thứ tự đầu vào model: [PM2.5, PM10, CO, NO2, O3]
    float virtual_sensors[5];
    
    virtual_sensors[0] = tvoc_raw;  // PM2.5 proxy
    virtual_sensors[1] = tvoc_raw;  // PM10 proxy
    virtual_sensors[2] = eco2_raw;  // CO proxy (tương quan mạnh với eCO2)
    virtual_sensors[3] = tvoc_raw;  // NO2 proxy
    virtual_sensors[4] = tvoc_raw;  // O3 proxy

    // 2. Cập nhật Sliding Window (Dịch mảng cũ sang trái 5 vị trí)
    for (int i = 0; i < 25; i++) {
        sliding_window[i] = sliding_window[i + 5];
    }
    
    // Đưa 5 giá trị mới vào cuối cửa sổ
    for (int i = 0; i < 5; i++) {
        sliding_window[25 + i] = virtual_sensors[i];
    }

    // 3. Chuẩn hóa bằng bộ hằng số MEAN và SCALE đã có
    for (int i = 0; i < 30; i++) {
        input->data.f[i] = (sliding_window[i] - AQI_MEAN[i]) / AQI_SCALE[i];
    }

    // 4. Chạy suy luận
    interpreter->Invoke();
    
    return output->data.f[0];
}