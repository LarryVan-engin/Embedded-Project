#include "blinky_thread.h"   // Hoặc header tương ứng trong project

//Chớp tắt LED xanh mỗi giây
// void blinky_thread_entry(void *argument)
// {
//     (void) argument;  // Không dùng argument

//     // Tắt LED ban đầu (nếu active low)
//     R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_06_PIN_09, BSP_IO_LEVEL_HIGH);

//     while (1)
//     {
//         // Bật LED xanh (active low: LOW = bật)
//         R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_06_PIN_09, BSP_IO_LEVEL_LOW);
//         R_BSP_SoftwareDelay(1000000, BSP_DELAY_UNITS_MICROSECONDS);   // ~1 giây (nếu tick = 10ms mặc định)

//         // Tắt LED
//         R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_06_PIN_09, BSP_IO_LEVEL_HIGH);
//         R_BSP_SoftwareDelay(1000000, BSP_DELAY_UNITS_MICROSECONDS);   // ~1 giây
//     }
// }

//Bấm nút thì bật, nhả nút thì tắt LED đỏ (không dùng ngắt)
void blinky_thread_entry(void *pvParameters)
{
    (void) pvParameters;  // Không dùng parameter

    bsp_io_level_t button_state = BSP_IO_LEVEL_HIGH; // Biến lưu trạng thái nút nhấn

    while (1)
    {
        // Đọc trạng thái nút nhấn (active low: LOW = bấm) P804
        //Active low: LOW = bấm, HIGH = nhả
        R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_08_PIN_04, &button_state);

        // Điều khiển LED đỏ (active low: LOW = bật)
        if (button_state == BSP_IO_LEVEL_LOW)
        {
            //Giữ LED đỏ bật khi nút được bấm
            R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_06_PIN_10, BSP_IO_LEVEL_HIGH); // Bật LED đỏ
        }
        else
        {
            R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_06_PIN_10, BSP_IO_LEVEL_LOW);
        }
        //Delay
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
