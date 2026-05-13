---
type: guide
status: verified
last_updated: 2026-04-25
---

# RCA_RTOS_Preemptive_Verification

Tags: #rtos #test #verification #preemption

## Kết quả quan sát

```
[RTOS TEST] Starting Preemptive Test...
[4 ms] [RTOS TEST] Preemptive Scheduling: PASS
```

## Phân tích: Kết quả có hợp lệ không?

**KẾT LUẬN: PASS là THẬT và có giá trị kỹ thuật.**

### Lý do kết luận PASS là xác thực

Cơ chế test được thiết kế dựa trên **shared volatile state** — biến `g_preempt_step` chỉ có thể bằng `2` khi và chỉ khi:

1. `LowPrioTask` gán `g_preempt_step = 1`
2. `LowPrioTask` gọi `OS_Task_Create()` cho `HighPrioTask`
3. **Ngay lập tức**, bên trong `OS_Task_Create()`, kernel gọi `OS_Schedule()`
4. `OS_Schedule()` phát hiện `HighPrioTask` (prio=4) > `LowPrioTask` (prio=5) → trigger `PendSV`
5. `PendSV_Handler` (assembly) lưu context `LowPrioTask`, restore context `HighPrioTask`
6. `HighPrioTask` chạy → `g_preempt_step = 2` → block `OS_Task_Delay(FOREVER)`
7. CPU trả về `LowPrioTask` → đọc `g_preempt_step == 2` → in PASS

> Nếu có bất kỳ bước nào sai (PendSV không trigger, stack corrupt, priority bitmap sai), biến sẽ không thể == 2, và test sẽ in `FAIL (step=1)` hoặc hệ thống sẽ crash HardFault.

### Những gì được xác nhận bởi bài test này

| Thành phần | Xác nhận |
|---|---|
| `OS_Task_Create()` | Tạo task thành công với stack hợp lệ |
| `OS_Schedule()` | Phát hiện đúng task ưu tiên cao nhất |
| `PendSV_Handler` | Context save/restore ARM Cortex-M33 đúng |
| Priority Bitmap (`os_ready_bitmap`) | Cập nhật bit chính xác khi task created |
| xPSR Thumb bit | Không bị HardFault → stack frame đúng |
| `OS_Task_Delay()` | Block task và yield CPU thành công |
| Stack alignment | Không có UsageFault → SP chia hết 8 |

### Tại sao không phải false positive?

- Nếu `OS_Task_Create()` **không** gọi `OS_Schedule()`, `LowPrioTask` tiếp tục chạy ngay, đọc `step==1` và in `FAIL`.
- Nếu PendSV bị disable (`__disable_irq()`), preemption không xảy ra → `FAIL`.
- Nếu stack frame sai, CPU sẽ crash khi restore PC → `HardFault` → không có output.
- Nếu EXC_RETURN sai (`0xFFFFFFF9` thay vì `0xFFFFFFFD`), CPU vào wrong privilege mode → crash.

### Bằng chứng bổ sung từ log thực tế

```
[0 ms] Tasks ready: LED1 blink, AHT20 logger, LED2 timer blink
[RTOS TEST] Starting Preemptive Test...
[4 ms] [RTOS TEST] Preemptive Scheduling: PASS
[80 ms] [sensor:0] T=26.8 C  RH=58.7%
[2163 ms] [sensor:1] T=26.8 C  RH=58.5%
[5000 ms] [timer] LED2 toggles=10
```

- **LED1 blink** (prio=4, 250ms): Hoạt động đúng → scheduler chạy đa nhiệm.
- **AHT20 logger** (prio=2, 2000ms): Đọc I2C thật → blocking I2C không hang hệ thống.
- **LED2 timer** (prio=3, 500ms): Software timer và semaphore hoạt động → Timer Daemon OK.
- **Preempt test** (prio=5): Tự suspend sau PASS → không gây nhiễu hệ thống.

## Các giới hạn và lưu ý

| Giới hạn | Ghi chú |
|---|---|
| **Single-shot test** | Chỉ test 1 lần khi boot. Không test tính ổn định dài hạn. |
| **Single-core** | Không có race condition thật trên RA6M5 (single-core). Trên multi-core cần mutex. |
| **Priority không đảo ngược** | Chưa test Priority Inversion (cần semaphore + 3 tasks với prio khác nhau). |
| **Stack depth** | Stack size mặc định. Nếu thêm floating-point context → cần tăng `OS_DEFAULT_STACK_SIZE`. |

## Bước tiếp theo sau khi RTOS PASS

1. ✅ **RTOS Preemptive Test: PASS**
2. 🔄 **AI Inference Test:** Chạy `test_ai_inference_init()` với synthetic data để verify TFLite pipeline.
3. ⬜ **AI + RTOS Integration Test:** Chạy `aqi_ai_predict()` trong một RTOS Task dùng dữ liệu thật từ AHT20.
4. ⬜ **Production:** Ghép inference vào `task_sensor_logger` với dữ liệu AHT20 thật.
