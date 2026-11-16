# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(pico-wspr-tx-enhanced C CXX ASM)

pico_sdk_init()
add_executable(pico-wspr-tx-enhanced)
pico_generate_pio_header(pico-wspr-tx-enhanced ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco/dco2.pio)
pico_generate_pio_header(pico-wspr-tx-enhanced ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco/dco3.pio)

# N2GF build options --------------------------------------------------------
# WSPR_SYSCLK_MHZ : system clock in MHz (empty = original 200 MHz). DCO max = sysclk/8.
#                   e.g. -DPICO_BOARD=pico2 -DWSPR_SYSCLK_MHZ=288 for 10 m on a Pico 2.
# WSPR_DCO_FINE   : 1 = dco3.pio back end (per-half-period counts, ~4x lower spurs),
#                   0 = original dco2.pio back end (default).
set(WSPR_SYSCLK_MHZ "" CACHE STRING "System clock in MHz (empty = original 200 MHz)")
set(WSPR_DCO_FINE 0 CACHE STRING "1 = fine dco3.pio DCO back end, 0 = original dco2.pio")
if (NOT "${WSPR_SYSCLK_MHZ}" STREQUAL "")
  target_compile_definitions(pico-wspr-tx-enhanced PRIVATE WSPR_SYSCLK_MHZ=${WSPR_SYSCLK_MHZ})
  message(STATUS "N2GF: WSPR_SYSCLK_MHZ=${WSPR_SYSCLK_MHZ}")
endif()
target_compile_definitions(pico-wspr-tx-enhanced PRIVATE WSPR_DCO_FINE=${WSPR_DCO_FINE})
message(STATUS "N2GF: WSPR_DCO_FINE=${WSPR_DCO_FINE}")

target_sources(pico-wspr-tx-enhanced PUBLIC
	             ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/lib/assert.c
               ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco/piodco.c
               ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/gpstime/GPStime.c
               ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/gpstime/ppscal.c
               ${CMAKE_CURRENT_LIST_DIR}/TxChannel/TxChannel.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty/WSPRutility.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty/nhash.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty/maidenhead.c
               ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/WSPRbeacon.c
               ${CMAKE_CURRENT_LIST_DIR}/debug/logutils.c
               ${CMAKE_CURRENT_LIST_DIR}/init.c
               ${CMAKE_CURRENT_LIST_DIR}/core1.c
               ${CMAKE_CURRENT_LIST_DIR}/main.c
               ${CMAKE_CURRENT_LIST_DIR}/persistentStorage.c
                ${CMAKE_CURRENT_LIST_DIR}/cw_beacon.c
              )

pico_set_program_name(pico-wspr-tx-enhanced "pico-wspr-tx-enhanced")
pico_set_program_version(pico-wspr-tx-enhanced "0.5")

pico_enable_stdio_uart(pico-wspr-tx-enhanced 1)
pico_enable_stdio_usb(pico-wspr-tx-enhanced 1)

# Add the standard include files to the build
target_include_directories(pico-wspr-tx-enhanced PRIVATE
                           ${CMAKE_CURRENT_LIST_DIR}
                           ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/piodco
                           ${CMAKE_CURRENT_LIST_DIR}/pico-hf-oscillator/debug
                           ${CMAKE_CURRENT_LIST_DIR}/TxChannel
                           ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon
                           ${CMAKE_CURRENT_LIST_DIR}/WSPRbeacon/thirdparty
                           ${CMAKE_CURRENT_LIST_DIR}/..
                          )

target_link_libraries(pico-wspr-tx-enhanced
                      pico_stdlib
                      hardware_flash
                      hardware_vreg
                      pico_sync
                      pico_multicore
                      hardware_timer
                      hardware_clocks
                      hardware_pio
                     )

pico_add_extra_outputs(pico-wspr-tx-enhanced)
