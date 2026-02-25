idf_component_register(
  SRCS "bsp_gps.c"
  INCLUDE_DIRS "include"
  REQUIRES driver esp_driver_uart esp_driver_gpio esp_timer
)
