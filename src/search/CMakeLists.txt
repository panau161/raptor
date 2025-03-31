# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.25...3.30)

if (TARGET raptor::search)
    return ()
endif ()

add_library ("raptor_search" STATIC raptor_search.cpp search_hibf.cpp search_ibf.cpp search_partitioned_ibf.cpp)
target_link_libraries ("raptor_search" PUBLIC "raptor::interface")

option (RAPTOR_FPGA_HARDWARE "Compile for FPGA hardware" OFF)
add_subdirectory (fpga)
if (TARGET raptor::fpga::interface)
    if (RAPTOR_FPGA_HARDWARE)
        target_link_libraries ("raptor_search" PUBLIC "raptor::fpga::hardware")
    else ()
        target_link_libraries ("raptor_search" PUBLIC "raptor::fpga::emulated")
    endif ()
endif ()

add_library (raptor::search ALIAS raptor_search)
