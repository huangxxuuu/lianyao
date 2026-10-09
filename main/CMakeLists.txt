idf_component_register(
    SRCS "main.c"
         "potion_assets.c"
         "potion_hash.c"
         "potion_model.c"
         "potion_radio.c"
         "potion_store.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash mbedtls
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/potion_font_16.c"
)

# Large player/save structures must not return to task stacks on this no-PSRAM board.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Werror=frame-larger-than=2048
)
