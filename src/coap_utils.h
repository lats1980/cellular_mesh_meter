/**
 * @file
 * @defgroup coap_client_utils API for coap_client_* samples
 * @{
 */

/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __COAP_METER_UTILS_H__
#define __COAP_METER_UTILS_H__

#include <openthread/coap.h>
#include <openthread/ip6.h>
#include <openthread/message.h>
#include <openthread/thread.h>

#define COAP_PORT OT_DEFAULT_COAP_PORT
#define PROVISIONING_URI_PATH "provisioning"
#define MODEM_URI_PATH "modem"

/**@brief Enumeration describing modem commands. */
enum modem_command {
	MODEM_COMMAND_DISCOVER,
	MODEM_COMMAND_REPORT_STATE
};

/** @brief Type indicates function called when OpenThread connection
 *         is established.
 *
 * @param[in] item pointer to work item.
 */
typedef void (*ot_connection_cb_t)(struct k_work *item);

/** @brief Type indicates function called when OpenThread connection is ended.
 *
 * @param[in] item pointer to work item.
 */
typedef void (*ot_disconnection_cb_t)(struct k_work *item);

/** @brief Type indicates function called when the MTD modes are toggled.
 *
 * @param[in] val 1 if the MTD is in MED mode
 *                0 if the MTD is in SED mode
 */
typedef void (*mtd_mode_toggle_cb_t)(uint32_t val);

/** @brief Initialize CoAP client utilities.
 */
void coap_client_utils_init(ot_connection_cb_t on_connect,
			    ot_disconnection_cb_t on_disconnect,
				mtd_mode_toggle_cb_t on_toggle);

/**
 * @brief Send CoAP request to report current modem state.
 */
otError coap_utils_modem_report_state(const otMessageInfo *message_info,
					  uint8_t modem_state);

/**
 * @brief Send CoAP response to modem report state request.
 */
otError coap_utils_modem_report_state_response(otMessage *request_message,
					  const otMessageInfo *message_info);

/** @brief Send CoAP Multicast request to discover available modems.
 *
 * @note CoAP server with available modem will send report state back.
 */
void coap_utils_modem_discover(void);

/** @brief Toggle SED to MED and MED to SED modes.
 *
 * @note Active when the device is working as Minimal Thread Device.
 */
void coap_client_toggle_minimal_sleepy_end_device(void);

typedef void (*modem_request_callback_t)(otMessage *message,
				  const otMessageInfo *message_info);
typedef void (*provisioning_request_callback_t)();

int ot_coap_init(provisioning_request_callback_t on_provisioning_request,
                 modem_request_callback_t on_modem_request);

#endif

/**
 * @}
 */
