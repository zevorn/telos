if(CONFIG_TELOS)
    zephyr_library()

    zephyr_include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/../../../include
    )

    include(${CMAKE_CURRENT_LIST_DIR}/generated_sources.cmake)

    zephyr_library_sources(
        ${CMAKE_CURRENT_LIST_DIR}/../src/runtime.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_TELOS_SHELL
        ${CMAKE_CURRENT_LIST_DIR}/../src/shell.c
    )
endif()
