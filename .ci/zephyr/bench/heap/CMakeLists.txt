# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(retained_heap_regression)
target_sources(app PRIVATE main.c ${ZEPHYR_BASE}/tests/lib/heap/src/main.c)
