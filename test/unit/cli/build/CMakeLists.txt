# SPDX-FileCopyrightText: 2006-2024 Knut Reinert & Freie Universität Berlin
# SPDX-FileCopyrightText: 2016-2024 Knut Reinert & MPI für molekulare Genetik
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.25...3.30)

raptor_add_unit_test (build_hibf_chopper_test.cpp)
raptor_add_unit_test (build_hibf_test.cpp)
raptor_add_unit_test (build_ibf_partitioned_test.cpp)
raptor_add_unit_test (build_ibf_test.cpp)
