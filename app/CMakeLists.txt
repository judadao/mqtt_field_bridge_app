cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mqtt_field_bridge_app)

target_sources(app PRIVATE
  src/main.c
  src/product_config.c
  src/product_runtime.c
  src/product_topics.c
  src/product_console.c
  src/product_status_io.c
  src/bridge_control.c
)

if(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
  target_sources(app PRIVATE src/product_wifi.c)
else()
  target_sources(app PRIVATE src/product_ethernet.c)
endif()

target_include_directories(app PRIVATE src)
