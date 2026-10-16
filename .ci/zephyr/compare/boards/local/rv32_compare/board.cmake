set(SUPPORTED_EMU_PLATFORMS custom)
if(NOT DEFINED COMPARE_ENGINE)
  message(FATAL_ERROR "COMPARE_ENGINE must name qemu or rv32emu")
endif()
if(NOT DEFINED ENV{ZEPHYR_COMPARE_RUNNER})
  message(FATAL_ERROR "Use .ci/compare-zephyr.sh to configure the comparison runner")
endif()
add_custom_target(run_custom
  COMMAND ${PYTHON_EXECUTABLE} "$ENV{ZEPHYR_COMPARE_RUNNER}"
          --engine "${COMPARE_ENGINE}"
          --elf "${APPLICATION_BINARY_DIR}/zephyr/zephyr.elf"
          --bin "${APPLICATION_BINARY_DIR}/zephyr/zephyr.bin"
  WORKING_DIRECTORY "${APPLICATION_BINARY_DIR}"
  DEPENDS ${logical_target_for_zephyr_elf}
  USES_TERMINAL
)
