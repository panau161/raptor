# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-FileCopyrightText: 2020-2025 Thomas Steinke & Zuse Institute Berlin
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.25...3.30)

# project(raptor_oneAPI_FPGA CXX)

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

# Shared Includes between all targets
add_library(raptor_fpga_interface INTERFACE)
target_link_libraries(raptor_fpga_interface INTERFACE "raptor::interface")
target_include_directories(raptor_fpga_interface SYSTEM INTERFACE "${RAPTOR_MIN_IBF_FPGA_ROOT}/include")
target_compile_definitions(raptor_fpga_interface INTERFACE RAPTOR_FPGA_SHARED_LIBRARY_BASE_PATH=\"${CMAKE_BINARY_DIR}\")
target_compile_options(raptor_fpga_interface INTERFACE "-fsycl" "-fintelfpga" "-Xshyper-optimized-handshaking=off" "-qactypes")
target_link_options(raptor_fpga_interface INTERFACE "-fsycl" "-fintelfpga" "-Xshyper-optimized-handshaking=off" "-qactypes")
add_library (raptor::fpga::interface ALIAS raptor_fpga_interface)

# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(HOST_SOURCE_FILE raptor_search_fpga_oneapi.cpp)
set(DEVICE_SOURCE_FILE "${RAPTOR_MIN_IBF_FPGA_ROOT}/src/fpga_device.cpp")
set(TARGET_NAME raptor_search_fpga_oneapi_lib)
set(EMULATOR_TARGET ${TARGET_NAME}.fpga_emu)
set(FPGA_TARGET ${TARGET_NAME}.fpga)

if(NOT DEFINED WINDOW_SIZE_LIST)
    message(STATUS "No WINDOW_SIZE_LIST supplied. Defaulting to '23'.")
    set(WINDOW_SIZE_LIST "23")
endif()

if(NOT DEFINED MIN_IBF_K_LIST)
    message(STATUS "No MIN_IBF_K_LIST supplied. Defaulting to '19'.")
    set(MIN_IBF_K_LIST "19")
endif()

if(NOT DEFINED BIN_COUNT_LIST)
    message(STATUS "No BIN_COUNT_LIST supplied. Defaulting to '64;8192'.")
    set(BIN_COUNT_LIST 64 8192)
endif()

# set(BIN_COUNT_LIST 64)
# list(JOIN BIN_COUNT_LIST "," BIN_COUNT_STRING)
# message(STATUS "BIN_COUNT_STRING: ${BIN_COUNT_STRING}")
# set(BIN_COUNT_LIST 64 8192)
# list(JOIN BIN_COUNT_LIST "," BIN_COUNT_STRING)
# message(STATUS "BIN_COUNT_STRING: ${BIN_COUNT_STRING}")

if(NOT DEFINED KERNEL_COPYS_LIST)
    message(STATUS "No KERNEL_COPYS_LIST supplied. Defaulting to '1;2'.")
    set(KERNEL_COPYS_LIST "1;2")
endif()

set(HARDWARE_LINK_FLAGS -Xshardware -Xstarget=${FPGA_DEVICE} ${USER_HARDWARE_FLAGS})
# use cmake -D USER_HARDWARE_FLAGS=<flags> to set extra flags for FPGA backend compilation

macro(GENERATE_TARGET RESULT_SUFFIX OUTPUT_VAR)
    set(${OUTPUT_VAR} "${TARGET_NAME}_kernel_w${WINDOW_SIZE}_k${MIN_IBF_K}_b${BIN_COUNT}_kernels${KERNEL_COPYS}.${RESULT_SUFFIX}")
endmacro()

macro(ADD_COMMON_TARGET_OPTIONS TARGET_TO)
    target_compile_options(${TARGET_TO} PRIVATE -DWINDOW_SIZE=${WINDOW_SIZE} -DMIN_IBF_K=${MIN_IBF_K} -DBIN_COUNT=${BIN_COUNT} -DKERNEL_COPYS=${KERNEL_COPYS})
    target_link_libraries (${TARGET_TO} PRIVATE raptor::fpga::interface)
endmacro()

macro(ITERATE_OVER_COMBINATIONS)
    foreach(WINDOW_SIZE IN LISTS WINDOW_SIZE_LIST)
        foreach(MIN_IBF_K IN LISTS MIN_IBF_K_LIST)
            if(${WINDOW_SIZE} GREATER ${MIN_IBF_K})
                foreach(BIN_COUNT IN LISTS BIN_COUNT_LIST)
                    foreach(KERNEL_COPYS IN LISTS KERNEL_COPYS_LIST)
                        # Evaluate the block of code passed as ARGN
                        message (STATUS "EVAL ${ARGN}")
                        # CMAKE_LANGUAGE(EVAL CODE "${ARGN}")
                        # foreach(CODE_LINE IN LISTS ${ARGN})
                        #     message (STATUS "EVALS ${CODE_LINE}")
                        #     CMAKE_LANGUAGE(EVAL CODE "${CODE_LINE}")
                        # endforeach()
                        CMAKE_LANGUAGE(EVAL CODE ${ARGN})
                    endforeach()
                endforeach()
            else()
                message(WARNING "Deselecting w${WINDOW_SIZE}_k${MIN_IBF_K} because window size is not bigger than k-mer size.")
            endif()
        endforeach()
    endforeach()
endmacro()



###############################################################################
### FPGA Emulator
###############################################################################
# To compile in a single command:
#    icpx -fsycl -fintelfpga -qactypes -DFPGA_EMULATOR fpga_compile.cpp -o fpga_compile.fpga_emu
# CMake executes:
#    [compile] icpx -fsycl -fintelfpga -qactypes -DFPGA_EMULATOR -o fpga_compile.cpp.o -c fpga_compile.cpp
#    [link]    icpx -fsycl -fintelfpga -qactypes fpga_compile.cpp.o -o fpga_compile.fpga_emu

add_library(${EMULATOR_TARGET} SHARED ${HOST_SOURCE_FILE})
target_link_libraries(${EMULATOR_TARGET} PRIVATE raptor::fpga::interface)
target_compile_definitions(${EMULATOR_TARGET} PRIVATE FPGA_EMULATOR)

ITERATE_OVER_COMBINATIONS([=[
    GENERATE_TARGET(fpga_emu EMULATOR_DEVICE_TARGET)
    add_library(\${EMULATOR_DEVICE_TARGET} SHARED \${DEVICE_SOURCE_FILE})
    ADD_COMMON_TARGET_OPTIONS(\${EMULATOR_DEVICE_TARGET})
    target_link_libraries(\${EMULATOR_TARGET} PRIVATE \${EMULATOR_DEVICE_TARGET})
]=])

###############################################################################
### Generate Report
###############################################################################

ITERATE_OVER_COMBINATIONS([=[
    GENERATE_TARGET(a FPGA_EARLY_IMAGE)
    # The compile output is not an executable, but an intermediate compilation result unique to SYCL.
    # fsycl-link=early stops the compiler after RTL generation, before invoking Quartus®
    add_executable(\${FPGA_EARLY_IMAGE} \${DEVICE_SOURCE_FILE})
    ADD_COMMON_TARGET_OPTIONS(\${FPGA_EARLY_IMAGE})
    target_link_options(\${FPGA_EARLY_IMAGE} PRIVATE \${HARDWARE_LINK_FLAGS} -fsycl-link=early)
]=])

###############################################################################
### FPGA Hardware
###############################################################################
# To compile in a single command:
#   icpx -fsycl -fintelfpga -qactypes -Xshardware -Xstarget=<FPGA_DEVICE> ac_fixed.cpp -o ac_fixed.fpga
# CMake executes:
#   [compile] icpx -fsycl -fintelfpga -qactypes -o ac_fixed.cpp.o -c ac_fixed.cpp
#   [link]    icpx -fsycl -fintelfpga -qactypes -Xshardware -Xstarget=<FPGA_DEVICE> ac_fixed.cpp.o -o ac_fixed.fpga

# add_library(${FPGA_TARGET} STATIC EXCLUDE_FROM_ALL ${HOST_SOURCE_FILE})
# target_link_libraries(${FPGA_TARGET} PRIVATE raptor::fpga::interface)
# target_compile_definitions(${FPGA_TARGET} PRIVATE FPGA_HARDWARE)

ITERATE_OVER_COMBINATIONS([=[
    GENERATE_TARGET(fpga FPGA_DEVICE_TARGET)
    add_library(\${FPGA_DEVICE_TARGET} SHARED EXCLUDE_FROM_ALL \${DEVICE_SOURCE_FILE})
    ADD_COMMON_TARGET_OPTIONS(\${FPGA_DEVICE_TARGET})
    target_link_options(\${FPGA_DEVICE_TARGET} PRIVATE \${HARDWARE_LINK_FLAGS})
    target_link_libraries (\${FPGA_TARGET} PRIVATE \${FPGA_DEVICE_TARGET})
]=])



