#
# Copyright (c) 2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(openthread_cellular_mesh_meter)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c
			   src/coap_meter_utils.c)
# NORDIC SDK APP END

target_sources_ifdef(CONFIG_MODEM_UTILS_SIMULATED app PRIVATE src/modem_utils_simulated.c)
target_sources_ifdef(CONFIG_MODEM_UTILS_SERIAL_LTE_MODEM app PRIVATE src/modem_utils_slm.c)

target_sources_ifdef(CONFIG_BT_NUS app PRIVATE src/ble_utils.c)
