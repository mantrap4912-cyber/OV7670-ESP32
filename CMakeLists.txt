# ESP-IDF component build for OV7670-ESP32-NonFIFO.
#
# Drop this directory into your project's components/ folder (or point
# EXTRA_COMPONENT_DIRS at it) and add "ov7670_esp32_nonfifo" to your
# component REQUIRES.
#
# For the experimental ESP32-S3 LCD_CAM back end, add to your project:
#   target_compile_definitions(${COMPONENT_LIB} PUBLIC OV7670_ENABLE_LCDCAM_EXPERIMENTAL)

idf_component_register(
    SRCS
        "src/sccb.cpp"
        "src/xclk.cpp"
        "src/ov7670_presets.cpp"
        "src/dvp_capture_i2s.cpp"
        "src/dvp_capture_lcdcam.cpp"
        "src/OV7670Camera.cpp"
    INCLUDE_DIRS
        "src"
    REQUIRES
        driver
        esp_timer
    PRIV_REQUIRES
        esp_rom
        hal
        soc
        freertos
        log
        esp_hw_support
)

# The unpack loops run once per line at up to 7 kHz; -O2 is worth it even in a
# size-optimised build.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
