---
type: guide
status: completed
last_updated: 2026-04-25
---

# Firmware TFLite Integration Guide

Tags: #tflite #ai #driver #tinyml

## Overview
Tài liệu này vạch ra quy trình chuẩn để tích hợp thư viện **TensorFlow Lite cho Microcontrollers (TFLite Micro)** vào các dự án baremetal hoặc RTOS trên vi điều khiển Renesas RA. Tài liệu này đóng vai trò là sách hướng dẫn cho việc triển khai các mô hình Machine Learning mới trong tương lai và cung cấp cẩm nang theo dõi (trace) các lỗi phổ biến.

---

## 1. Directory Structure Setup (Cấu trúc thư mục)
Khi muốn đưa TFLite Micro sang một dự án mới:
- **KHÔNG** copy toàn bộ clone của `tflite-micro` repo.
- **HÃY** copy một phiên bản "portable" đã được lược bỏ các thành phần thừa. Chỉ sao chép các thư mục: `tensorflow/lite/micro/`, `tensorflow/lite/c/`, `tensorflow/lite/core/`, `tensorflow/lite/schema/`, và `third_party/`.
- **RẤT QUAN TRỌNG:** Phải xóa tất cả các file test (`*_test.cc`, `*_test.c`) để ngăn CMake `GLOB_RECURSE` biên dịch hàng tá hàm `main()` gây lỗi multiple definition.

---

## 2. Implementing the Inference Wrapper (Triển khai Wrapper)
Luôn đóng gói các API C++ của TFLite Micro bên trong một thư viện bọc (wrapper) bằng `extern "C"`. Điều này đảm bảo code inference có thể được gọi an toàn từ mã nguồn C chuẩn như `main.c` hay các RTOS Tasks.

### Các thành phần cốt lõi:
1. `tflite::GetModel(g_model_data)`: Đọc và xác thực cấu trúc mô hình dạng mảng.
2. `tflite::AllOpsResolver`: Nạp toàn bộ các toán tử nơ-ron có thể dùng. 
   - *Tối ưu:* Để tiết kiệm bộ nhớ flash, có thể dùng `MicroMutableOpResolver` và chỉ `Add()` những toán tử mô hình bạn dùng (vd: AddConv1D, AddDense).
3. `tflite::MicroInterpreter`: Đối tượng chính thực thi mô hình.
4. `tensor_arena`: Mảng tĩnh bằng `uint8_t` phục vụ cấp phát vùng nhớ trung gian cho TFLite. Bắt buộc phải có `alignas(16)`.

### Mã nguồn mẫu (Boilerplate)
```cpp
constexpr int kTensorArenaSize = 16 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

// Initialize
model = tflite::GetModel(g_model_data);
static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
interpreter = &static_interpreter;
interpreter->AllocateTensors();

// Predict
input->data.f[i] = value;
interpreter->Invoke();
return output->data.f[0];
```

---

## 3. Pre-processing & Feature Scaling (Tiền xử lý dữ liệu)
Các mô hình huấn luyện bằng Python (sử dụng Scikit-learn như `StandardScaler` hay `MinMaxScaler`) yêu cầu dữ liệu phải được scale tại vi điều khiển **trước khi** đưa vào mô hình.

- Định nghĩa sẵn bộ hằng số `MEAN` (Trung bình) và `SCALE` (Độ lệch chuẩn) (vd: `Scaler_Constants.h`).
- Sử dụng Ring Buffer hoặc thao tác dịch mảng (Shift Buffer) nếu mô hình yêu cầu chuỗi thời gian lịch sử (VD: cần 6 giờ đọc cảm biến trước đó).
- Scale dữ liệu theo công thức: `input = (raw_value - MEAN) / SCALE` và gán vào tensor.

---

## 4. Common Errors & Tracing (Lỗi phổ biến & Cách gỡ lỗi)
Sử dụng hàm `MicroPrintf()` (hoặc cấu hình lại `#define TF_LITE_STRIP_ERROR_STRINGS 0`) để xem lỗi khi chạy debug.

| Lỗi gặp phải | Nguyên nhân & Cách khắc phục |
|-------------|-----------------------------|
| **`AllocateTensors() failed`** | Vùng nhớ `kTensorArenaSize` cấp phát quá nhỏ không đủ lưu tensor trung gian. Tăng kích thước lên 2KB hoặc 4KB mỗi lần cho đến khi thành công. Xem file .map để cân đối lại vùng RAM. |
| **`Model schema version mismatch`** | Phiên bản Python converter tạo file tflite không đồng bộ với phiên bản thư viện TFLite C++ Micro đang chạy. Cần convert lại model. |
| **`Input/Output type mismatch`** | Thường xảy ra khi model bị quantized (đưa về `int8`) nhưng mã C lại trích xuất bằng mảng `float` (hoặc ngược lại). Kiểm tra thông số `optimizations` khi convert model ở python. |
| **`Invoke failed / Missing Ops`** | Nếu dùng `MicroMutableOpResolver` nhưng quên `Add()` toán tử (operation) nào đó. Thay tạm bằng `AllOpsResolver` để xác nhận. |
| **HardFault ngay khi khởi động** | Quên từ khóa `alignas(16)` khi định nghĩa `tensor_arena`. Vi điều khiển Cortex-M lỗi alignment truy cập bộ nhớ. |

---

## 5. Build System (CMake) — `cmake/GeneratedSrc.cmake`

Key rules:
- Compiler flags required: `-std=c++14`, `-fno-rtti`, `-fno-exceptions`, `-fno-threadsafe-statics`.
- Two separate source lists are used:
  - `Source_Files` — project code (`src/`, `Driver/`, `BSP/`) with full warnings enabled.
  - `TFLite_Source_Files` — TFLite core compiled with `-w` to silence third-party warnings.
- Include directories required for TFLite:
  - `Middleware/TensorFlowLite` — root for `tensorflow/lite/...` includes
  - `Middleware/TensorFlowLite/third_party/flatbuffers/include`
  - `Middleware/TensorFlowLite/third_party/gemmlowp`
- **Do NOT** add `Middleware/TensorFlowLite/**` to the project `Source_Files` GLOB — TFLite has its own `TFLite_Source_Files` group with special `-w` per-file properties.
- **Do NOT** use `AllOpsResolver` — it was removed in newer TFLite versions.
- **Do NOT** include the full `micro_mutable_op_resolver.h` header — it pulls in `signal/irfft.h` → KissFFT. Instead include `kernels/micro_ops.h` and call `AddFullyConnected()` / `AddRelu()` directly.

---

## 6. CMake GLOB_RECURSE — Danh sách đầy đủ lỗi và cách fix

> **Nguyên tắc cốt lõi:** TFLite Micro repo chứa rất nhiều code chỉ chạy trên PC (tools, tests, Python bindings, platform ports riêng biệt). Khi dùng `GLOB_RECURSE` để lấy toàn bộ `*.cc`, CMake sẽ kéo vào các file này và gây lỗi build hàng loạt. Giải pháp là dùng `list(FILTER ... EXCLUDE REGEX ...)` để loại bỏ chúng.

### 6.1 Bảng lỗi theo thứ tự phát sinh

| # | File lỗi (pattern) | Lỗi nhận được | Nguyên nhân | Filter cần thêm |
|---|---|---|---|---|
| 1 | `third_party/ruy/*_test.cc` | `gtest/gtest.h: No such file` | Unit test của Ruy cần Google Test (PC-only) | `.*_test\.cc$` |
| 2 | `third_party/ruy/benchmark.cc` | `gtest/gtest.h: No such file` | Benchmark cần GTest | `.*benchmark.*` |
| 3 | `third_party/ruy/blocking_counter.cc` | `condition_variable` / `mutex` không tồn tại | Ruy dùng `std::thread` — không có trên bare-metal | `.*third_party.*` |
| 4 | `experimental/microfrontend/` | `kiss_fft.h: No such file` | Module audio dùng KissFFT (chưa được tải về) | `.*experimental.*` |
| 5 | `micro/arc_custom/`, `arc_emsdp/` | `arc/arc_timer.h: No such file` | Code riêng cho chip ARC của Synopsys | `.*arc_custom.*` |
| 6 | `micro/arc_emsdp/debug_log.cc` | `eyalroz_printf/src/printf/printf.h` | Thiếu thư viện printf cho ARC platform | `.*arc_emsdp.*` |
| 7 | `micro/integration_tests/seanet/` | `python/tflite_micro/python_ops_resolver.h` | Integration test cần Python resolver | `.*integration_tests.*` |
| 8 | `micro/micro_mutable_op_resolver.h` | `signal/micro/kernels/irfft.h` | Signal ops (audio DSP) — cần thư mục `signal/` | Copy `signal/` từ repo gốc |
| 9 | `signal/src/irfft_int16.cc` | `kiss_fft.h: No such file` | Signal ops phụ thuộc KissFFT | `.*signal.*` |
| 10 | `micro/benchmarks/keyword_benchmark_8bit.cc` | `keyword_scrambled_8bit_model_data.h` | Benchmark cần model data file | `.*benchmark.*` (đổi từ `.*benchmark\.cc$`) |
| 11 | `micro/python/tflite_size/src/` | `Python.h: No such file` | Python binding — PC-only tool | `.*python.*` |
| 12 | `micro/kernels/test_data_generation/` | `multiple definition of 'main'` | Mỗi file có `main()` riêng — xung đột với `main.c` | `.*test_data_generation.*` |

### 6.2 Bộ filter hoàn chỉnh (copy-paste vào CMake)

```cmake
# === TFLite Source Filter — Loại bỏ tất cả code PC-only ===
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*_test\\.cc$")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*_test\\.c$")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*benchmark.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*third_party.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*examples.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*testing.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*testdata.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*test_data_generation.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*integration_tests.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*experimental.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*tools.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*signal.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*python.*")
list(FILTER TFLite_Source_Files EXCLUDE REGEX ".*(arc_custom|arc_emsdp|arc_mli|bluepill|ceva|chre|cortex_m_corstone_300|cortex_m_generic|ethos_u|hexagon|riscv32_generic|xtensa|cmsis_nn|models).*")

# Tắt warnings từ TFLite (third-party code)
foreach(tflite_src ${TFLite_Source_Files})
    set_source_files_properties(${tflite_src} PROPERTIES COMPILE_FLAGS "-w")
endforeach()
```

### 6.3 Include directories bắt buộc

```cmake
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/third_party/flatbuffers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Middleware/TensorFlowLite/third_party/gemmlowp
)
```

### 6.4 Third-party dependencies cần tải thủ công

TFLite Micro **không** đi kèm source code của các thư viện phụ thuộc. Cần tải và đặt vào đúng vị trí:

| Thư viện | URL | Đích |
|---|---|---|
| FlatBuffers | `github.com/google/flatbuffers/archive/refs/tags/v25.9.23.zip` | `third_party/flatbuffers/include/` |
| Gemmlowp | `github.com/google/gemmlowp/archive/719139ce.zip` | `third_party/gemmlowp/` (lấy `fixedpoint/`, `internal/`, `public/`) |
| Ruy | `github.com/google/ruy/archive/d37128311b.zip` | `third_party/ruy/` (headers only, không compile) |
