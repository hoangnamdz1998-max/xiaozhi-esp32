# Common source files for all targets
set(COMMON_SRCS
    "src/network_error.cc"
    "src/esp/esp_network.cc"
    "src/esp/esp_ssl.cc"
    "src/esp/esp_tcp.cc"
    "src/esp/esp_mqtt.cc"
    "src/esp/esp_udp.cc"
    "src/web_socket.cc"
    "src/http_client.cc"
)

idf_component_register(
    SRCS
        ${COMMON_SRCS}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "."
    REQUIRES
        "esp_driver_gpio"
        "esp_driver_uart"
        "esp_pm"
        "esp-tls"
        "pthread"
        "mqtt"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-format)
