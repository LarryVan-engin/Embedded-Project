---
type: guide
status: completed
last_updated: 2026-04-25
---

# FW_RTOS_Test

Tags: #rtos #test #firmware

## Overview
Tài liệu này cung cấp hướng dẫn về việc thực hiện các bài kiểm thử tự động (Automated Tests) liên quan đến nhân RTOS, cụ thể là kiểm tra cơ chế **Preemptive Scheduling** (hoán đổi ngữ cảnh ưu tiên). Khác với test bare-metal thông thường, RTOS test yêu cầu phải tạo thành một Task chạy trong hệ thống.

## Preemptive Test Logic
Cơ chế test (được cài đặt trong `src/test/test_rtos.c`) dựa trên việc kiểm tra xem một Task có độ ưu tiên cao có lập tức giành được quyền CPU khi nó vừa được tạo ra bởi một Task ưu tiên thấp hay không.

### Các bước của kịch bản test:
1. `LowPrioTask` khởi động, in ra `[RTOS TEST] Starting Preemptive Test...`.
2. `LowPrioTask` gán một biến toàn cục `g_preempt_step = 1`.
3. `LowPrioTask` gọi hàm `OS_Task_Create()` để sinh ra `HighPrioTask` (có độ ưu tiên cao hơn).
4. Ngay tại khoảnh khắc `OS_Task_Create` kết thúc, hàm này bên trong gọi `OS_Schedule()`. Do `HighPrioTask` có độ ưu tiên cao hơn, `PendSV` sẽ được kích hoạt ngay lập tức.
5. CPU bị preempt. `HighPrioTask` bắt đầu chạy. Nó kiểm tra `g_preempt_step == 1`, và gán `g_preempt_step = 2`. Sau đó nó tự block vô tận (`OS_Task_Delay(OS_WAIT_FOREVER)`).
6. CPU được trả lại cho `LowPrioTask`.
7. `LowPrioTask` chạy dòng code tiếp theo sau `OS_Task_Create()`. Nó kiểm tra giá trị `g_preempt_step`. Nếu bằng 2, chứng tỏ Preemption đã xảy ra thành công trước đó. Nó in ra `PASS` qua UART và tự block vô tận.

## Hướng dẫn Trace Lỗi (Troubleshooting)
Nếu bài test báo `FAIL` (hoặc hệ thống bị treo), dưới đây là các bước Trace lỗi:

### 1. Test in ra `FAIL (step = 1)`
**Nguyên nhân:** CPU không nhảy sang `HighPrioTask` mà tiếp tục chạy `LowPrioTask`.
**Cách khắc phục:**
- Kiểm tra hàm `OS_Schedule()`. Xem biến `os_ready_bitmap` có được cập nhật bit của HighPrioTask không.
- Kiểm tra bit `SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;`. Lệnh gọi PendSV có bị bỏ qua do ngắt PendSV bị vô hiệu hóa (`__disable_irq()`) hay không.
- Chắc chắn rằng hàm `OS_Task_Create` kết thúc bằng việc gọi `OS_Schedule()`.

### 2. HardFault ngay sau khi OS_Task_Create
**Nguyên nhân:** Lỗi Context Switch (Lưu/Phục hồi trạng thái sai).
**Cách khắc phục:**
- Kiểm tra lại hàm tạo khung Stack (`OS_Task_Create`). Đảm bảo thanh ghi `xPSR` có bit T (Thumb state) được set = 1 (`0x01000000`).
- Kiểm tra địa chỉ PC (`task_entry`) và `EXC_RETURN` (`0xFFFFFFFD` cho luồng cơ bản) đã đúng offset chưa.
- Lỗi alignment của Stack Pointer. Vi điều khiển ARM yêu cầu SP phải chia hết cho 8 khi bắt đầu ngắt.

### 3. Hệ thống im lặng, không in ra gì cả
**Nguyên nhân:** Lỗi treo trong `PendSV_Handler` hoặc UART bị kẹt.
**Cách khắc phục:**
- Dùng Debugger Pause lại xem CPU đang đứng ở đâu. Nếu đứng ở `Default_Handler`, thì tra thanh ghi `CFSR` (Configurable Fault Status Register).
- Kiểm tra độ ưu tiên của ngắt UART. Nếu ưu tiên ngắt UART thấp hơn PendSV, nó có thể bị chặn. Luôn đảm bảo `PendSV` có mức ưu tiên **thấp nhất** (ưu tiên = 255).
