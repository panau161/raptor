# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-FileCopyrightText: 2020-2025 Thomas Steinke & Zuse Institute Berlin
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.25...3.30)

set_source_files_properties (${HOST_SOURCE_FILE} PROPERTIES COMPILE_DEFINITIONS
                            "FPGA_BINS=${BIN_COUNT_STRING}")

###############################################################################
### FPGA Emulator
###############################################################################
# To compile in a single command:
#    icpx -fsycl -fintelfpga -qactypes -DFPGA_EMULATOR fpga_compile.cpp -o fpga_compile.fpga_emu
# CMake executes:
#    [compile] icpx -fsycl -fintelfpga -qactypes -DFPGA_EMULATOR -o fpga_compile.cpp.o -c fpga_compile.cpp
#    [link]    icpx -fsycl -fintelfpga -qactypes fpga_compile.cpp.o -o fpga_compile.fpga_emu

add_library (${EMULATOR_TARGET} SHARED ${HOST_SOURCE_FILE})
target_link_libraries (${EMULATOR_TARGET} PRIVATE raptor::fpga::interface)
target_compile_definitions (${EMULATOR_TARGET} PRIVATE FPGA_EMULATOR)

###############################################################################
### Generate Report
###############################################################################

# See loop.

###############################################################################
### FPGA Hardware
###############################################################################
# To compile in a single command:
#   icpx -fsycl -fintelfpga -qactypes -Xshardware -Xstarget=<FPGA_DEVICE> ac_fixed.cpp -o ac_fixed.fpga
# CMake executes:
#   [compile] icpx -fsycl -fintelfpga -qactypes -o ac_fixed.cpp.o -c ac_fixed.cpp
#   [link]    icpx -fsycl -fintelfpga -qactypes -Xshardware -Xstarget=<FPGA_DEVICE> ac_fixed.cpp.o -o ac_fixed.fpga

add_library (${FPGA_TARGET} STATIC EXCLUDE_FROM_ALL ${HOST_SOURCE_FILE})
target_link_libraries (${FPGA_TARGET} PRIVATE raptor::fpga::interface)
target_compile_definitions (${FPGA_TARGET} PRIVATE FPGA_HARDWARE)

###############################################################################
### The Loop
###############################################################################

macro (GENERATE_TARGET RESULT_SUFFIX OUTPUT_VAR)
    set (${OUTPUT_VAR} "${TARGET_NAME}_kernel_w${WINDOW_SIZE}_k${MIN_IBF_K}_b${BIN_COUNT}_kernels${KERNEL_COPYS}.${RESULT_SUFFIX}")
endmacro ()

macro (ADD_COMMON_TARGET_OPTIONS TARGET_TO)
    target_compile_options (${TARGET_TO} PRIVATE -DWINDOW_SIZE=${WINDOW_SIZE} -DMIN_IBF_K=${MIN_IBF_K} -DBIN_COUNT=${BIN_COUNT} -DKERNEL_COPYS=${KERNEL_COPYS})
    target_link_libraries (${TARGET_TO} PRIVATE raptor::fpga::interface)
endmacro ()

foreach (WINDOW_SIZE IN LISTS WINDOW_SIZE_LIST)
    foreach (MIN_IBF_K IN LISTS MIN_IBF_K_LIST)
        if (${WINDOW_SIZE} GREATER ${MIN_IBF_K})
            foreach (BIN_COUNT IN LISTS BIN_COUNT_LIST)
                foreach (KERNEL_COPYS IN LISTS KERNEL_COPYS_LIST)
                    ## Emulator
                    generate_target (fpga_emu EMULATOR_DEVICE_TARGET)
                    add_library (${EMULATOR_DEVICE_TARGET} SHARED ${DEVICE_SOURCE_FILE})
                    add_common_target_options (${EMULATOR_DEVICE_TARGET})
                    target_link_libraries (${EMULATOR_TARGET} PRIVATE ${EMULATOR_DEVICE_TARGET})

                    ## Report
                    generate_target (a FPGA_EARLY_IMAGE)
                    # The compile output is not an executable, but an intermediate compilation result unique to SYCL.
                    # fsycl-link=early stops the compiler after RTL generation, before invoking Quartus®
                    add_executable (${FPGA_EARLY_IMAGE} ${DEVICE_SOURCE_FILE})
                    add_common_target_options (${FPGA_EARLY_IMAGE})
                    target_link_options (${FPGA_EARLY_IMAGE} PRIVATE ${HARDWARE_LINK_FLAGS} -fsycl-link=early)

                    ## FPGA
                    generate_target (fpga FPGA_DEVICE_TARGET)
                    add_library (${FPGA_DEVICE_TARGET} SHARED EXCLUDE_FROM_ALL ${DEVICE_SOURCE_FILE})
                    add_common_target_options (${FPGA_DEVICE_TARGET})
                    target_link_options (${FPGA_DEVICE_TARGET} PRIVATE ${HARDWARE_LINK_FLAGS})
                    target_link_libraries (${FPGA_TARGET} PRIVATE ${FPGA_DEVICE_TARGET})
                endforeach ()
            endforeach ()
        else ()
            message (WARNING "Deselecting w${WINDOW_SIZE}_k${MIN_IBF_K} because window size is not bigger than k-mer size.")
        endif ()
    endforeach ()
endforeach ()
