#ifndef PRODUCT_CONSOLE_H
#define PRODUCT_CONSOLE_H

#include <stddef.h>

typedef void (*product_console_write_fn)(void *ctx, const char *text);
typedef void (*product_console_reboot_fn)(void *ctx);

int product_console_handle_line(char *line,
                                product_console_write_fn write_fn,
                                void *write_ctx,
                                product_console_reboot_fn reboot_fn,
                                void *reboot_ctx);
void product_console_start(void);

#endif /* PRODUCT_CONSOLE_H */
