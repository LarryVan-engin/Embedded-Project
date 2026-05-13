#ifndef TEST_USB_H
#define TEST_USB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create the USB Device CDC logging task. */
void test_usb_cdc_logger_init(void);

/* Create the USB Host CDC descriptor test task. */
void test_usb_host_descriptor_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_USB_H */
