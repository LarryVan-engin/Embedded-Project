#ifndef IAQ_PREDICTOR_H
#define IAQ_PREDICTOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo mô hình AI (Cấp phát bộ nhớ, load model)
// Trả về true nếu thành công, false nếu lỗi
bool IAQ_AI_Init(void);

// Đưa dữ liệu TVOC vào mô hình để dự báo IAQ Rating (1.0 - 5.0)
// tvoc_ppb: Giá trị đọc thô từ cảm biến
float IAQ_AI_Predict(float tvoc_ppb);

#ifdef __cplusplus
}
#endif

#endif // IAQ_PREDICTOR_H