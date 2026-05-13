#ifndef APP_TEST_CONFIG_H
#define APP_TEST_CONFIG_H

/* USB integration tests disabled. */
#define OS_USB_TEST_MODE_NONE             0U

/* USB device-mode CDC logger task enabled. */
#define OS_USB_TEST_MODE_DEVICE_CDC       1U

/* USB host-mode descriptor enumeration task enabled. */
#define OS_USB_TEST_MODE_HOST_DESCRIPTOR  2U

/*
 * Select one USB integration test mode:
 *   NONE             : USB tests disabled
 *   DEVICE_CDC       : RTOS task publishes periodic CDC log frames
 *   HOST_DESCRIPTOR  : One-shot host enumeration test logs SIM descriptors
 */
#ifndef OS_USB_TEST_MODE
#define OS_USB_TEST_MODE                  OS_USB_TEST_MODE_NONE
#endif

/*
 * Enable the one-shot RTOS task that erases, writes, reads, and verifies
 * the final 64-byte Data Flash block reserved as a scratch area.
 */
#ifndef OS_FLASH_NVS_TEST_ENABLE
#define OS_FLASH_NVS_TEST_ENABLE          1U
#endif

#define SAFE_RESET_SELF_TEST  0   /* set to 0 for production */
#define PREEMTIVE_RTOS_TEST   0   /* set to 0 for production */

#endif /* APP_TEST_CONFIG_H */
