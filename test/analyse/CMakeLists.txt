# SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.20...3.31)
project (raptor_test_analyze CXX)

include (../raptor-test.cmake)

enable_testing ()

add_subdirectory (../performance performance)
add_subdirectory (../snippet snippet)
add_subdirectory (../unit unit)
