#ifndef ZEPHYR_LOGGING_LOG_H
#define ZEPHYR_LOGGING_LOG_H

#include <stdio.h>

#define LOG_MODULE_REGISTER(name, level)

#ifdef MQTT_TEST_VERBOSE
#define LOG_INF(fmt, ...)  printf("[INF] " fmt "\n", ##__VA_ARGS__)
#define LOG_WRN(fmt, ...)  printf("[WRN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)  fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)  printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_INF(fmt, ...)  do {} while (0)
#define LOG_WRN(fmt, ...)  do {} while (0)
#define LOG_ERR(fmt, ...)  do {} while (0)
#define LOG_DBG(fmt, ...)  do {} while (0)
#endif

#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4

#endif /* ZEPHYR_LOGGING_LOG_H */
