idf_component_register(
  SRCS "app_main.c" "sys_vision.c" "sys_audio.c" "sys_env.c" "sys_power.c"
  INCLUDE_DIRS "."
  REQUIRES bsp_camera bsp_audio bsp_env bsp_gps bsp_storage esp_timer mbedtls
)
