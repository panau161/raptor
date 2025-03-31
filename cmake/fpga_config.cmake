if (NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "IntelLLVM")
    target_compile_definitions (raptor_interface INTERFACE RAPTOR_FPGA=0)
    return ()
endif ()

if (TARGET raptor::fpga::interface)
    return ()
endif ()

target_compile_definitions (raptor_interface INTERFACE RAPTOR_FPGA=1)

set (RAPTOR_MIN_IBF_FPGA_ROOT "" CACHE STRING "")
if (NOT EXISTS "${RAPTOR_MIN_IBF_FPGA_ROOT}/include/min_ibf_fpga/")
    message (FATAL_ERROR "RAPTOR_MIN_IBF_FPGA_ROOT (\"${RAPTOR_MIN_IBF_FPGA_ROOT}\") must point to the root directory of the Raptor FPGA implementation.")
endif ()

# Try to detect FPGA board by searching for known boards in the output of "aoc -list-boards"
if (NOT DEFINED FPGA_DEVICE)
    set (known_boards "ofs_ia840f_usm" "ofs_d5005_usm")

    execute_process (COMMAND aoc -list-boards
                     OUTPUT_VARIABLE output
                     OUTPUT_STRIP_TRAILING_WHITESPACE
                     ERROR_QUIET)

    foreach (search_string IN LISTS known_boards)
        string (FIND "${output}" "${search_string}" string_pos)

        # If string is found
        if (NOT string_pos EQUAL -1)
            message (STATUS "Found FPGA board '${search_string}' in the output of 'aoc -list-boards'")
            set (FPGA_DEVICE "${search_string}")
            break ()
        endif ()
    endforeach ()
endif ()

# Fall back to pre defined device for FPGA board selection
if (NOT DEFINED FPGA_DEVICE)
    set (FPGA_DEVICE "intel_s10sx_pac:pac_s10_usm")
    message (STATUS "FPGA_DEVICE was not specified.\n"
                    "   Configuring the design to run on the default FPGA board ${FPGA_DEVICE}.\n"
                    "   Please refer to the README for information on board selection.")
else ()
    message (STATUS "Configuring the design to run on FPGA board ${FPGA_DEVICE}")
endif ()

# Shared Includes between all targets
add_library (raptor_fpga_interface INTERFACE)
target_link_libraries (raptor_fpga_interface INTERFACE "raptor::interface")
target_include_directories (raptor_fpga_interface SYSTEM INTERFACE "${RAPTOR_MIN_IBF_FPGA_ROOT}/include")
target_compile_options (raptor_fpga_interface INTERFACE "-fsycl" "-fintelfpga" "-Xshyper-optimized-handshaking=off" "-qactypes")
target_link_options (raptor_fpga_interface INTERFACE "-fsycl" "-fintelfpga" "-Xshyper-optimized-handshaking=off" "-qactypes")
add_library (raptor::fpga::interface ALIAS raptor_fpga_interface)

if (NOT DEFINED WINDOW_SIZE_LIST)
    set (WINDOW_SIZE_LIST 23)
    message (STATUS "No WINDOW_SIZE_LIST supplied. Defaulting to '${WINDOW_SIZE_LIST}'.")
endif ()
list (JOIN WINDOW_SIZE_LIST "," WINDOW_SIZE_STRING)

if (NOT DEFINED MIN_IBF_K_LIST)
    set (MIN_IBF_K_LIST 19)
    message (STATUS "No MIN_IBF_K_LIST supplied. Defaulting to '${MIN_IBF_K_LIST}'.")
endif ()
list (JOIN MIN_IBF_K_LIST "," MIN_IBF_K_STRING)

if (NOT DEFINED BIN_COUNT_LIST)
    set (BIN_COUNT_LIST 64 8192)
    message (STATUS "No BIN_COUNT_LIST supplied. Defaulting to '${BIN_COUNT_LIST}'.")
endif ()
list (JOIN BIN_COUNT_LIST "," BIN_COUNT_STRING)

if (NOT DEFINED KERNEL_COPYS_LIST)
    set (KERNEL_COPYS_LIST 1 2)
    message (STATUS "No KERNEL_COPYS_LIST supplied. Defaulting to '${KERNEL_COPYS_LIST}'.")
endif ()
list (JOIN KERNEL_COPYS_LIST "," KERNEL_COPYS_STRING)

set (HARDWARE_LINK_FLAGS -Xshardware -Xstarget=${FPGA_DEVICE} ${USER_HARDWARE_FLAGS})
# use cmake -D USER_HARDWARE_FLAGS=<flags> to set extra flags for FPGA backend compilation
