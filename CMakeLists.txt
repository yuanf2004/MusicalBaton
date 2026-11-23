# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# Couple PCB wiring and console configuration; leave the DK build untouched.
if(DTC_OVERLAY_FILE MATCHES "(^|[ ;/])musical_baton\\.overlay($|[ ;])")
  list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/musical_baton.conf")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(MusicalBaton)

target_sources(app PRIVATE
  src/main.c
  src/practice.c
  src/sensor.c
  src/conducting.c
  src/bluetooth.c
  src/device_name.c
  src/name_protocol.c
  src/button.c
  src/battery.c
  src/led.c
)

if(DTC_OVERLAY_FILE MATCHES "evaluation[.]overlay")
  target_compile_definitions(app PRIVATE BATON_EVAL_VECTORS)
endif()
target_link_libraries(app PRIVATE m)
