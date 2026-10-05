cmake_minimum_required(VERSION 3.16)

# ==============================================================================
# Tactical Multi-GNSS Terminal & Embedded Kalman Engine
# ANSI C99 Bare-Metal Core Library and Verification Harness
# ==============================================================================

project(gnss_tactical
    VERSION 1.0.0
    DESCRIPTION "Tactical Multi-GNSS Terminal & Zero-Heap Embedded Kalman Engine"
    LANGUAGES C
)

# Standard C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build Options
option(BUILD_TESTS "Build verification test harnesses" ON)
option(ENABLE_STRICT_WARNINGS "Treat compiler warnings as errors" ON)

# Compiler Warnings Configuration
if(MSVC)
    add_compile_options(/W4 /permissive-)
    if(ENABLE_STRICT_WARNINGS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -pedantic)
    if(ENABLE_STRICT_WARNINGS)
        add_compile_options(-Werror)
    endif()
endif()

# Global Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Core Firmware Source Files
set(TACTICAL_SOURCES
    src/battery_thermal_guard.c
    src/cgpx_engine.c
    src/chord_fsm.c
    src/cryo_sentinel.c
    src/crypto_hal.c
    src/fdcan_hal.c
    src/gnss_nmea.c
    src/hybrid_comms.c
    src/iridium_at_fsm.c
    src/kalman_filter.c
    src/mip_display.c
    src/nlos_filter.c
    src/power_supervisor.c
    src/split_node_bus.c
    src/sx1262_hal.c
    src/thermal_throttle.c
    src/topo_map.c
    src/trn_validator.c
    src/watchdog_tpl5010.c
)

# ==============================================================================
# Static Library Target: libgnss_tactical.a
# ==============================================================================
add_library(gnss_tactical STATIC ${TACTICAL_SOURCES})
target_include_directories(gnss_tactical PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Math library linking for POSIX
if(UNIX AND NOT APPLE)
    target_link_libraries(gnss_tactical PUBLIC m)
endif()

# ==============================================================================
# Verification Test Harnesses & CTest Integration
# ==============================================================================
if(BUILD_TESTS)
    enable_testing()

    macro(add_tactical_test TARGET_NAME SOURCE_FILE)
        add_executable(${TARGET_NAME} ${SOURCE_FILE})
        target_link_libraries(${TARGET_NAME} PRIVATE gnss_tactical)
        if(UNIX AND NOT APPLE)
            target_link_libraries(${TARGET_NAME} PRIVATE m)
        endif()
    endmacro()

    # Phase 1: CGPX Engine
    add_tactical_test(test_harness_cgpx tests/test_harness.c)
    add_test(NAME test_harness_cgpx
        COMMAND test_harness_cgpx ${CMAKE_CURRENT_SOURCE_DIR}/data/mont_blanc_sample.cgpx
    )

    # Phase 2: MIP Display & Chording FSM
    add_tactical_test(test_harness_display tests/test_harness_display.c)
    add_test(NAME test_harness_display COMMAND test_harness_display)

    # Phase 3: Split-Node CAN-FD Bus
    add_tactical_test(test_harness_bus tests/test_harness_bus.c)
    add_test(NAME test_harness_bus COMMAND test_harness_bus)

    # Phase 4: GNSS Tokenizer, Skymask & Crypto HAL
    add_tactical_test(test_harness_nav tests/test_harness_nav.c)
    add_test(NAME test_harness_nav COMMAND test_harness_nav)

    # Phase 5: Sub-Zero Power & Watchdog
    add_tactical_test(test_harness_power tests/test_harness_power.c)
    add_test(NAME test_harness_power COMMAND test_harness_power)

    # Phase 6: Topographic Terrain Engine
    add_tactical_test(test_harness_topo tests/test_harness_topo.c)
    add_test(NAME test_harness_topo COMMAND test_harness_topo)

    # Cryo Sentinel Guard
    add_tactical_test(test_harness_cryo tests/test_harness_cryo.c)
    add_test(NAME test_harness_cryo COMMAND test_harness_cryo)

    # Phase 7: Hybrid Tactical Comms & BFT
    add_tactical_test(test_harness_hybrid_comms tests/test_harness_hybrid_comms.c)
    add_test(NAME test_harness_hybrid_comms COMMAND test_harness_hybrid_comms)

    # Kinematic Extended Kalman Filter
    add_tactical_test(test_harness_kalman tests/test_harness_kalman.c)
    add_test(NAME test_harness_kalman COMMAND test_harness_kalman)

    # STM32 Silicon FDCAN Hardware Layer
    add_tactical_test(test_harness_fdcan tests/test_harness_fdcan.c)
    add_test(NAME test_harness_fdcan COMMAND test_harness_fdcan)

    # Physical RF Hardware Drivers (SX1262 LoRa SPI & RockBLOCK Iridium SBD AT FSM)
    add_tactical_test(test_harness_radio tests/test_harness_radio.c)
    add_test(NAME test_harness_radio COMMAND test_harness_radio)
endif()

# ==============================================================================
# Installation Rules
# ==============================================================================
install(TARGETS gnss_tactical
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
