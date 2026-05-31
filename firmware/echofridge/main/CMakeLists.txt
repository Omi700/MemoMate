idf_component_register(
    SRCS
        "main.cpp"
        "sd_card.cpp"
        "poem_data.cpp"
        "fonts/anton_82_time.c"
        "fonts/anton_86_time.c"
        "fonts/anton_42_climate.c"
        "fonts/anton_90_time.c"
        "fonts/anton_92_time.c"
        "fonts/anton_28_climate.c"
        "fonts/montserrat_21_month.c"
        "fonts/montserrat_24_climate.c"
        "fonts/lxgw_wenkai_12_poem.c"
        "fonts/lxgw_wenkai_14_poem.c"
        "fonts/lxgw_wenkai_16_poem.c"
        "fonts/lxgw_wenkai_17_poem.c"
        "fonts/lxgw_wenkai_18_poem.c"
    REQUIRES
        esp_timer
        esp_driver_sdmmc
        fatfs
        sdmmc
        json
        port_bsp
        u8g2_st7305
    INCLUDE_DIRS
        "."
        "fonts"
    EMBED_FILES
        "../../../papers/poems.json"
)

set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(BUILD_TIME_HEADER "${GENERATED_INCLUDE_DIR}/build_time.h")

add_custom_target(generate_build_time_header ALL
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_FILE=${BUILD_TIME_HEADER}
        -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/generate_build_time.cmake
    BYPRODUCTS ${BUILD_TIME_HEADER}
)

add_dependencies(${COMPONENT_LIB} generate_build_time_header)
target_include_directories(${COMPONENT_LIB} PRIVATE "${GENERATED_INCLUDE_DIR}")
