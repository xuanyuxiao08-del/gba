idf_component_register(
    SRCS 
        "main.c"
        "display.c"
        "audio.c"
        "sdcard.c"
        "vrom.c"
    INCLUDE_DIRS "."
    REQUIRES driver fatfs vfs heap esp_hw_support gba_core
)