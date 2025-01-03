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
 * @brief Send CoAP request to update modem state.
 */
otError coap_client_send_modem_update_state(const otMessageInfo *message_info,
					  uint8_t modem_state);

/**
 * @brief Send CoAP response to modem update state request.
 */
otError coap_server_send_modem_update_state_response(otMessage *request_message,
					  const otMessageInfo *message_info);

/** @brief Multicast request to discover available modems.
 *
 * @note CoAP server with available modem will send Update State request back.
 */
void coap_client_send_modem_discover_request(void);

/** @brief Toggle SED to MED and MED to SED modes.
 *
 * @note Active when the device is working as Minimal Thread Device.
 */
void coap_client_toggle_minimal_sleepy_end_device(void);

#define COAP_PORT OT_DEFAULT_COAP_PORT
#define PROVISIONING_URI_PATH "provisioning"
#define MODEM_URI_PATH "modem"

/**@brief Enumeration describing modem commands. */
enum modem_command {
	THREAD_COAP_UTILS_MODEM_CMD_DISCOVER,
	THREAD_COAP_UTILS_MODEM_CMD_UPDATE_STATE_IDLE,
	THREAD_COAP_UTILS_MODEM_CMD_UPDATE_STATE_BUSY
};

typedef void (*modem_request_callback_t)(otMessage *message,
				  const otMessageInfo *message_info);
typedef void (*provisioning_request_callback_t)();

int ot_coap_init(provisioning_request_callback_t on_provisioning_request,
                 modem_request_callback_t on_modem_request);

#endif

/**
 * @}
 */
