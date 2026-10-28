set(BSP_SRCS
    "ap_wifi/ap_wifi.c"
    "ap_wifi/wifi_manager.c"
    "ap_wifi/ws_server.c"
    "lcd/st7789_i80.c"
    "lcd/ft6336u_driver.c"
    "xl9555/xl9555.c"
    "light/light.c"
    "light/led_ws2812.c"
)

idf_component_register(
    SRCS ${BSP_SRCS}
    INCLUDE_DIRS
        "."
        "ap_wifi"
        "lcd"
        "xl9555"
        "light"
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_rmt
        esp_driver_ledc
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        spiffs
        lwip
    PRIV_REQUIRES
        espressif__cjson
)

