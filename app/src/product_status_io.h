#ifndef PRODUCT_STATUS_IO_H
#define PRODUCT_STATUS_IO_H

#include <stdint.h>

typedef void (*product_status_button_fn)(void *ctx);

int product_status_io_init(product_status_button_fn power_long_press_fn,
                           void *power_ctx,
                           product_status_button_fn config_reset_fn,
                           void *config_reset_ctx);
void product_status_io_set_running(uint8_t running);
void product_status_io_set_error(void);
void product_status_io_record_activity(void);

#endif /* PRODUCT_STATUS_IO_H */
