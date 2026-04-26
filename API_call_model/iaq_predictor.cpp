#include "iaq_predictor.h"
#include "iaq_model_data.h"   // Mảng Hex chứa trọng số của model AI
#include "scaler_constants.h" // Module chứa hằng số chuẩn hóa (MEAN, SCALE) tự động sinh

// Thư viện TensorFlow Lite Micro
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
// #include "tensorflow/lite/version.h"

// --- CẤU HÌNH BỘ NHỚ (TENSOR ARENA) ---
// Mạng MLP 32-16-1 khá nhẹ, 4KB là đủ. Nếu bị lỗi AllocateTensors, hãy tăng lên 8*1024
constexpr int kTensorArenaSize = 4 * 1024;
alignas(16) static uint8_t tensor_arena[kTensorArenaSize];

// --- BIẾN TOÀN CỤC ---
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

// ============================================================================
// HÀM 1: KHỞI TẠO MÔ HÌNH AI (Gọi 1 lần trong hal_entry trước vòng lặp while)
// ============================================================================
bool IAQ_AI_Init(void) {
    // 1. Ánh xạ model từ mảng hằng số (Flash)
    model = tflite::GetModel(g_iaq_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        // Lỗi: Phiên bản model không tương thích với cấu trúc TFLM hiện tại
        return false;
    }

    // 2. Khai báo các toán tử (Operations) mà mạng MLP sử dụng
    // Tối ưu hóa bộ nhớ: Chỉ nạp đúng 2 Ops (Dense và ReLU) thay vì AllOpsResolver
    static tflite::MicroMutableOpResolver<2> resolver;
    resolver.AddFullyConnected(); // Cho các layer Dense
    resolver.AddRelu();           // Cho hàm kích hoạt ReLU

    // 3. Khởi tạo Interpreter (Bộ suy luận cốt lõi của TFLM)
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // 4. Cấp phát bộ nhớ cho các tensor (Input, Output, Hidden layers)
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        return false; // Lỗi cấp phát bộ nhớ (Có thể do kTensorArenaSize quá nhỏ)
    }

    // 5. Lấy con trỏ của vùng Input và Output để dùng cho hàm Predict
    input = interpreter->input(0);
    output = interpreter->output(0);

    return true;
}

// ============================================================================
// HÀM 2: DỰ BÁO IAQ TƯƠNG LAI (Gọi liên tục trong vòng lặp đọc cảm biến)
// ============================================================================
float IAQ_AI_Predict(float tvoc_ppb) {
    if (interpreter == nullptr) {
        return -1.0f; // Chưa khởi tạo Model thành công
    }

    // 1. TIỀN XỬ LÝ (Chuẩn hóa Z-score sử dụng hằng số OTA)
    // IAQ_MEAN và IAQ_SCALE được nạp từ file scaler_constants.h
    float tvoc_scaled = (tvoc_ppb - IAQ_MEAN[0]) / IAQ_SCALE[0];

    // 2. NẠP DỮ LIỆU ĐẦU VÀO
    input->data.f[0] = tvoc_scaled;

    // 3. SUY LUẬN (Chạy mạng Neural)
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        return -1.0f; // Lỗi trong quá trình tính toán ma trận
    }

    // 4. TRẢ KẾT QUẢ
    float iaq_forecast = output->data.f[0];
    
    // Giới hạn kết quả trả về luôn nằm trong khung chuẩn UBA (1.0 - 5.0)
    if (iaq_forecast < 1.0f) iaq_forecast = 1.0f;
    if (iaq_forecast > 5.0f) iaq_forecast = 5.0f;

    return iaq_forecast;
}