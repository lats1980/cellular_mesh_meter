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
#define METER_URI_PATH "meter"
#define MODEM_URI_PATH "modem"

/**@brief Enumeration describing modem commands. */
enum modem_command {
	MODEM_COMMAND_DISCOVER,
	MODEM_COMMAND_REPORT_STATE,
	MODEM_COMMAND_UPLOAD_MEASUREMENT,
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

/** @brief Initialize CoAP client utilities.
 */
void coap_client_utils_init(ot_connection_cb_t on_connect,
			    			ot_disconnection_cb_t on_disconnect);

/** @brief Send CoAP Multicast request to discover available modems.
 *
 * @note CoAP server with available modem will send report state back.
 */
void coap_utils_modem_discover(void);

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

/**
 * @brief Send CoAP request to upload measurement to the remote modem.
 */
otError coap_utils_modem_upload_measurement(const otMessageInfo *message_info);

/**
 * @brief Send CoAP response with given response code.
 */
otError coap_utils_send_response(otMessage *request_message,
								 const otMessageInfo *message_info,
								 otCoapCode code);

/**
 * @brief Callback function for modem request.
 */
typedef void (*modem_request_callback_t)(otMessage *message,
										 const otMessageInfo *message_info);

/**
 * @brief Callback function for meter block transmission.
 */
typedef void (*meter_block_tx_callback_t)(void     *aContext,
										  uint8_t  *aBlock,
										  uint32_t  aPosition,
										  uint16_t *aBlockLength,
										  bool     *aMore);

/**
 * @brief Callback function for meter block reception.
 */
typedef otError (*meter_block_rx_callback_t)(void *aContext,
											 const uint8_t *aBlock,
											 uint32_t aPosition,
											 uint16_t aBlockLength,
											 bool aMore,
											 uint32_t aTotalLength);

/**
 * @brief Callback function for meter response.
 */
typedef void (*meter_response_callback_t)(void *context, otMessage *message, const otMessageInfo *message_info, otError error);

/**
 * @brief Initialize CoAP server utilities.
 */
int ot_coap_init(modem_request_callback_t on_modem_request,
				 meter_block_tx_callback_t on_meter_block_tx,
				 meter_block_rx_callback_t on_meter_block_rx,
				 meter_response_callback_t on_meter_response);

#endif

/**
 * @}
 */
