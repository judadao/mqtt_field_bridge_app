cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mqtt_field_bridge_app)

find_program(NODE_EXECUTABLE node)
if(NOT NODE_EXECUTABLE)
  message(FATAL_ERROR "node is required to generate embedded provisioning UI")
endif()

set(PROVISIONING_WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../deps/dephy_web/products/mqtt_field_bridge/web)
set(PROVISIONING_WEB_HTML ${PROVISIONING_WEB_DIR}/index.html)
set(PROVISIONING_WEB_CSS ${PROVISIONING_WEB_DIR}/styles.css)
set(PROVISIONING_WEB_JS ${PROVISIONING_WEB_DIR}/app.js)
set(PROVISIONING_WEB_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/provisioning_index.h)
set(PROVISIONING_WEB_EMBED ${CMAKE_CURRENT_SOURCE_DIR}/../deps/dephy_web/tools/embed_web.js)

add_custom_command(
  OUTPUT ${PROVISIONING_WEB_HEADER}
  COMMAND ${NODE_EXECUTABLE} ${PROVISIONING_WEB_EMBED}
          ${PROVISIONING_WEB_DIR}
          ${PROVISIONING_WEB_HEADER}
          index_html
  DEPENDS ${PROVISIONING_WEB_HTML}
          ${PROVISIONING_WEB_CSS}
          ${PROVISIONING_WEB_JS}
          ${PROVISIONING_WEB_EMBED}
  COMMENT "Generating embedded provisioning UI"
)

add_custom_target(provisioning_web_asset DEPENDS ${PROVISIONING_WEB_HEADER})

target_sources(app PRIVATE
  src/main.c
  src/product_config.c
  src/product_runtime.c
  src/product_topics.c
  src/product_console.c
  src/product_ethernet.c
  src/bridge_control.c
  src/provisioning_http.c
)

target_include_directories(app PRIVATE src)
add_dependencies(app provisioning_web_asset)
