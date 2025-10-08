#!/usr/bin/env bash

# JIT Debug Test Script
# This script tests JIT compiler with debug mode enabled to catch issues early

set -e

PARALLEL="${PARALLEL:--j$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)}"

echo "======================================"
echo "JIT Debug Mode Test"
echo "======================================"

# Test 1: Standard JIT with debug
echo ""
echo "Test 1: Building with ENABLE_JIT_DEBUG=1..."
make distclean
make ENABLE_JIT=1 ENABLE_JIT_DEBUG=1 $PARALLEL

echo ""
echo "Running basic tests with JIT debug..."
make ENABLE_JIT=1 ENABLE_JIT_DEBUG=1 check

# Test 2: JIT with EXT_C=0 and debug (regression test)
echo ""
echo "Test 2: Building with ENABLE_EXT_C=0 ENABLE_JIT_DEBUG=1..."
make distclean
make ENABLE_EXT_C=0 ENABLE_JIT=1 ENABLE_JIT_DEBUG=1 $PARALLEL

echo ""
echo "Running tests with EXT_C=0 and JIT debug..."
make ENABLE_EXT_C=0 ENABLE_JIT=1 ENABLE_JIT_DEBUG=1 check

# Test 3: JIT with various extension combinations
echo ""
echo "Test 3: Testing multiple JIT configurations with debug..."
for config in \
    "ENABLE_EXT_A=0" \
    "ENABLE_EXT_F=0" \
    "ENABLE_EXT_M=0" \
    "ENABLE_Zba=0" \
    "ENABLE_Zbb=0"; do
    echo ""
    echo "Testing: $config with JIT debug"
    make distclean
    make $config ENABLE_JIT=1 ENABLE_JIT_DEBUG=1 $PARALLEL
    make $config ENABLE_JIT=1 ENABLE_JIT_DEBUG=1 check
done

echo ""
echo "======================================"
echo "All JIT debug tests passed!"
echo "======================================"
