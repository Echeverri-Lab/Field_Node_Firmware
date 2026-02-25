idf_component_register(
  SRCS "bsp_storage.c"
  INCLUDE_DIRS "include"
  REQUIRES driver fatfs sdmmc esp_timer
)
