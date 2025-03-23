# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.25...3.30)

project(raptor_oneAPI_FPGA CXX)

set (RAPTOR_MIN_IBF_FPGA_ROOT "" CACHE STRING "")
if (NOT EXISTS "${RAPTOR_MIN_IBF_FPGA_ROOT}/include/min_ibf_fpga/")
    message(FATAL_ERROR "RAPTOR_MIN_IBF_FPGA_ROOT (\"${RAPTOR_MIN_IBF_FPGA_ROOT}\") must point to the root directory of the Raptor FPGA implementation.")
endif ()

# Try to detect FPGA board by searching for known boards in the output of "aoc -list-boards"
if(NOT DEFINED FPGA_DEVICE)
    set(known_boards "ofs_ia840f_usm" "ofs_d5005_usm")

    execute_process(
        COMMAND aoc -list-boards
        OUTPUT_VARIABLE output
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    foreach(search_string IN LISTS known_boards)
        string(FIND "${output}" "${search_string}" string_pos)

        # If string is found
        if(NOT string_pos EQUAL -1)
            message(STATUS "Found FPGA board '${search_string}' in the output of 'aoc -list-boards'")
            set(FPGA_DEVICE "${search_string}")
            break()
        endif()
    endforeach()
endif()

# Fall back to pre defined device for FPGA board selection
if(NOT DEFINED FPGA_DEVICE)
    set(FPGA_DEVICE "intel_s10sx_pac:pac_s10_usm")
    message(STATUS "FPGA_DEVICE was not specified.\
                    \nConfiguring the design to run on the default FPGA board ${FPGA_DEVICE}.\
                    \nPlease refer to the README for information on board selection.")
else()
    message(STATUS "Configuring the design to run on FPGA board ${FPGA_DEVICE}")
endif()

# add_subdirectory(${RAPTOR_MIN_IBF_FPGA_ROOT} EXCLUDE_FROM_ALL min-ibf-fpga-oneapi)

# Shared Includes between all targets
add_library(raptor_search_fpga_oneapi_base INTERFACE)
target_link_libraries(raptor_search_fpga_oneapi_base INTERFACE "raptor::interface")
target_include_directories(raptor_search_fpga_oneapi_base INTERFACE "${RAPTOR_MIN_IBF_FPGA_ROOT}/include")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(HOST_SOURCE_FILE raptor_search_fpga_oneapi.cpp)
set(DEVICE_SOURCE_FILE "${RAPTOR_MIN_IBF_FPGA_ROOT}/src/fpga_device.cpp")
set(TARGET_NAME raptor_search_fpga_oneapi_lib)
set(EMULATOR_TARGET ${TARGET_NAME}.fpga_emu)
set(FPGA_TARGET ${TARGET_NAME}.fpga)

if(NOT DEFINED WINDOW_SIZE_LIST)
    message("No WINDOW_SIZE_LIST supplied. Defaulting to '23'.")
    set(WINDOW_SIZE_LIST "23")
endif()

if(NOT DEFINED MIN_IBF_K_LIST)
    message("No MIN_IBF_K_LIST supplied. Defaulting to '19'.")
    set(MIN_IBF_K_LIST "19")
endif()

if(NOT DEFINED BIN_COUNT_LIST)
    message("No BIN_COUNT_LIST supplied. Defaulting to '64;8192'.")
    set(BIN_COUNT_LIST "64;8192")
endif()

if(NOT DEFINED KERNEL_COPYS_LIST)
    message("No KERNEL_COPYS_LIST supplied. Defaulting to '1;2'.")
    set(KERNEL_COPYS_LIST "1;2")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 2022)
    set(BOARD_FLAG "-Xstarget")
    set(EMULATOR_AC_INT_FLAG "-qactypes")
else()
    set(BOARD_FLAG "-Xsboard")
endif()

set(COMMON_COMPILE_FLAGS -fsycl -fintelfpga -Xshyper-optimized-handshaking=off -Wall)
set(COMMON_LINK_FLAGS -fsycl -fintelfpga -Xshyper-optimized-handshaking=off)
set(HARDWARE_LINK_FLAGS -Xshardware ${BOARD_FLAG}=${FPGA_DEVICE} ${USER_HARDWARE_FLAGS})
# use cmake -D USER_HARDWARE_FLAGS=<flags> to set extra flags for FPGA backend compilation

###############################################################################
### FPGA Emulator
###############################################################################
# To compile in a single command:
#    icpx -fsycl -fintelfpga -qactypes -DFPGA_EMULATOR fpga_compile.cpp -o fpga_compile.fpga_emu
# CMake executes:
#    [compile] icpx -fsycl -fintelfpga -qactypes -DFPGA_EMULATOR -o fpga_compile.cpp.o -c fpga_compile.cpp
#    [link]    icpx -fsycl -fintelfpga -qactypes fpga_compile.cpp.o -o fpga_compile.fpga_emu

add_library(${EMULATOR_TARGET} SHARED ${HOST_SOURCE_FILE})
target_link_libraries(${EMULATOR_TARGET} PRIVATE raptor_search_fpga_oneapi_base)
target_compile_definitions(${EMULATOR_TARGET} PRIVATE -DFPGA_EMULATOR)
target_compile_options(${EMULATOR_TARGET} PRIVATE ${COMMON_COMPILE_FLAGS} ${EMULATOR_AC_INT_FLAG})
target_link_options(${EMULATOR_TARGET} PRIVATE ${COMMON_LINK_FLAGS} ${EMULATOR_AC_INT_FLAG})
export(TARGETS ${EMULATOR_TARGET} NAMESPACE "raptor::fpga::" FILE "raptor_seach_fpga_oneapi_targets.cmake")

add_custom_target(raptor_fpga_emu DEPENDS ${EMULATOR_TARGET})

foreach(WINDOW_SIZE ${WINDOW_SIZE_LIST})
    foreach(MIN_IBF_K ${MIN_IBF_K_LIST})
        if(${WINDOW_SIZE} GREATER ${MIN_IBF_K})
            foreach(BIN_COUNT ${BIN_COUNT_LIST})
                foreach(KERNEL_COPYS ${KERNEL_COPYS_LIST})
                    set(EMULATOR_DEVICE_TARGET ${TARGET_NAME}_kernel_w${WINDOW_SIZE}_k${MIN_IBF_K}_b${BIN_COUNT}_kernels${KERNEL_COPYS}.fpga_emu)
                    add_library(${EMULATOR_DEVICE_TARGET} SHARED ${DEVICE_SOURCE_FILE})
                    target_include_directories(${EMULATOR_DEVICE_TARGET} PRIVATE "${RAPTOR_MIN_IBF_FPGA_ROOT}/include")
                    target_compile_definitions(${EMULATOR_DEVICE_TARGET} PRIVATE -DWINDOW_SIZE=${WINDOW_SIZE} -DMIN_IBF_K=${MIN_IBF_K} -DBIN_COUNT=${BIN_COUNT} -DKERNEL_COPYS=${KERNEL_COPYS})
                    target_compile_options(${EMULATOR_DEVICE_TARGET} PRIVATE ${COMMON_COMPILE_FLAGS} ${EMULATOR_AC_INT_FLAG})
                    target_link_options(${EMULATOR_DEVICE_TARGET} PRIVATE ${COMMON_LINK_FLAGS} ${EMULATOR_AC_INT_FLAG})
                    add_dependencies(raptor_fpga_emu ${EMULATOR_DEVICE_TARGET})
                endforeach()
            endforeach()
        else()
            message(WARNING "Deselecting w${WINDOW_SIZE}_k${MIN_IBF_K} because window size is not bigger than k-mer size.")
        endif()
    endforeach()
endforeach()

###############################################################################
### Generate Report
###############################################################################

foreach(WINDOW_SIZE ${WINDOW_SIZE_LIST})
    foreach(MIN_IBF_K ${MIN_IBF_K_LIST})
        if(${WINDOW_SIZE} GREATER ${MIN_IBF_K})
            foreach(BIN_COUNT ${BIN_COUNT_LIST})
                foreach(KERNEL_COPYS ${KERNEL_COPYS_LIST})
                    set(FPGA_EARLY_IMAGE ${TARGET_NAME}_kernel_w${WINDOW_SIZE}_k${MIN_IBF_K}_b${BIN_COUNT}_kernels${KERNEL_COPYS}_report.a)
                    # The compile output is not an executable, but an intermediate compilation result unique to SYCL.
                    add_executable(${FPGA_EARLY_IMAGE} ${DEVICE_SOURCE_FILE})
                    target_include_directories(${FPGA_EARLY_IMAGE} PRIVATE "${RAPTOR_MIN_IBF_FPGA_ROOT}/include")
                    target_compile_definitions(${FPGA_EARLY_IMAGE} PRIVATE -DWINDOW_SIZE=${WINDOW_SIZE} -DMIN_IBF_K=${MIN_IBF_K} -DBIN_COUNT=${BIN_COUNT} -DKERNEL_COPYS=${KERNEL_COPYS})
                    target_compile_options(${FPGA_EARLY_IMAGE} PRIVATE ${COMMON_COMPILE_FLAGS} -qactypes)
                    target_link_options(${FPGA_EARLY_IMAGE} PRIVATE ${COMMON_LINK_FLAGS} ${HARDWARE_LINK_FLAGS} -fsycl-link=early)
                    # fsycl-link=early stops the compiler after RTL generation, before invoking Quartus®
                endforeach()
            endforeach()
        endif()
    endforeach()
endforeach()

###############################################################################
### FPGA Hardware
###############################################################################
# To compile in a single command:
#   icpx -fsycl -fintelfpga -qactypes -Xshardware -Xstarget=<FPGA_DEVICE> ac_fixed.cpp -o ac_fixed.fpga
# CMake executes:
#   [compile] icpx -fsycl -fintelfpga -qactypes -o ac_fixed.cpp.o -c ac_fixed.cpp
#   [link]    icpx -fsycl -fintelfpga -qactypes -Xshardware -Xstarget=<FPGA_DEVICE> ac_fixed.cpp.o -o ac_fixed.fpga

add_library(${FPGA_TARGET} SHARED EXCLUDE_FROM_ALL ${HOST_SOURCE_FILE})
target_link_libraries(${FPGA_TARGET} PRIVATE raptor_search_fpga_oneapi_base)
target_compile_definitions(${FPGA_TARGET} PRIVATE -DFPGA_HARDWARE)
target_compile_options(${FPGA_TARGET} PRIVATE ${COMMON_COMPILE_FLAGS} -qactypes)
target_link_options(${FPGA_TARGET} PRIVATE ${COMMON_LINK_FLAGS} -qactypes)
export(TARGETS ${FPGA_TARGET} NAMESPACE "raptor::fpga::" APPEND FILE "raptor_seach_fpga_oneapi_targets.cmake")

add_custom_target(raptor_fpga DEPENDS ${FPGA_TARGET})

foreach(WINDOW_SIZE ${WINDOW_SIZE_LIST})
    foreach(MIN_IBF_K ${MIN_IBF_K_LIST})
        if(${WINDOW_SIZE} GREATER ${MIN_IBF_K})
            foreach(BIN_COUNT ${BIN_COUNT_LIST})
                foreach(KERNEL_COPYS ${KERNEL_COPYS_LIST})
                    set(FPGA_DEVICE_TARGET ${TARGET_NAME}_kernel_w${WINDOW_SIZE}_k${MIN_IBF_K}_b${BIN_COUNT}_kernels${KERNEL_COPYS}.fpga)
                    add_library(${FPGA_DEVICE_TARGET} SHARED EXCLUDE_FROM_ALL ${DEVICE_SOURCE_FILE})
                    target_include_directories(${FPGA_DEVICE_TARGET} PRIVATE "${RAPTOR_MIN_IBF_FPGA_ROOT}/include")
                    target_compile_definitions(${FPGA_DEVICE_TARGET} PRIVATE -DWINDOW_SIZE=${WINDOW_SIZE} -DMIN_IBF_K=${MIN_IBF_K} -DBIN_COUNT=${BIN_COUNT} -DKERNEL_COPYS=${KERNEL_COPYS})
                    target_compile_options(${FPGA_DEVICE_TARGET} PRIVATE ${COMMON_COMPILE_FLAGS} -qactypes)
                    target_link_options(${FPGA_DEVICE_TARGET} PRIVATE ${COMMON_LINK_FLAGS} -qactypes ${HARDWARE_LINK_FLAGS})
                    add_dependencies(raptor_fpga ${FPGA_DEVICE_TARGET})
                endforeach()
            endforeach()
        else()
            message(WARNING "Deselecting w${WINDOW_SIZE}_k${MIN_IBF_K} because window size is not bigger than k-mer size.")
        endif()
    endforeach()
endforeach()



