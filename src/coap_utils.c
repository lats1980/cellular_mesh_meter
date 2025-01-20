/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <net/coap_utils.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <zephyr/net/socket.h>

#include "coap_utils.h"

LOG_MODULE_REGISTER(cellular_mesh_meter_util, CONFIG_CELLULAR_MESH_METER_UTILS_LOG_LEVEL);

static bool is_connected;

#define COAP_WORKQ_STACK_SIZE 2048
#define COAP_WORKQ_PRIORITY 5

K_THREAD_STACK_DEFINE(coap_client_workq_stack_area, COAP_WORKQ_STACK_SIZE);
static struct k_work_q coap_client_workq;

static struct k_work modem_discover_work;
static struct k_work on_connect_work;
static struct k_work on_disconnect_work;

/* Options supported by the server */
static const char *const modem_option[] = { MODEM_URI_PATH, NULL };
static const char *const provisioning_option[] = { PROVISIONING_URI_PATH,
						   NULL };

/* Thread multicast mesh local address */
static struct sockaddr_in6 multicast_local_addr = {
	.sin6_family = AF_INET6,
	.sin6_port = htons(COAP_PORT),
	.sin6_addr.s6_addr = { 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
	.sin6_scope_id = 0U
};

/* Variable for storing server address acquiring in provisioning handshake */
static char unique_local_addr_str[INET6_ADDRSTRLEN];
static struct sockaddr_in6 unique_local_addr = {
	.sin6_family = AF_INET6,
	.sin6_port = htons(COAP_PORT),
	.sin6_addr.s6_addr = {0, },
	.sin6_scope_id = 0U
};

struct server_context {
	struct otInstance *ot;
	bool provisioning_enabled;
	modem_request_callback_t on_modem_request;
	provisioning_request_callback_t on_provisioning_request;
};

static struct server_context srv_context = {
	.ot = NULL,
	.provisioning_enabled = false,
	.on_modem_request = NULL,
	.on_provisioning_request = NULL,
};

/**@brief Definition of CoAP resources for provisioning. */
static otCoapResource provisioning_resource = {
	.mUriPath = PROVISIONING_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

/**@brief Definition of CoAP resources for modem. */
static otCoapResource modem_resource = {
	.mUriPath = MODEM_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

static otError provisioning_response_send(otMessage *request_message,
					  const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;

	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		goto end;
	}

	otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE,
			  OT_COAP_CODE_CONTENT);

	error = otCoapMessageSetToken(
		response, otCoapMessageGetToken(request_message),
		otCoapMessageGetTokenLength(request_message));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	payload = otThreadGetMeshLocalEid(srv_context.ot);
	payload_size = sizeof(otIp6Address);

	error = otMessageAppend(response, payload, payload_size);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendResponse(srv_context.ot, response, message_info);

	LOG_HEXDUMP_INF(payload, payload_size, "Sent provisioning response:");

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

	return error;
}

otError coap_utils_modem_report_state_response(otMessage *request_message,
					  const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;

	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		goto end;
	}

	error = otCoapMessageInitResponse(response, request_message, OT_COAP_TYPE_ACKNOWLEDGMENT,
									  OT_COAP_CODE_CHANGED);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendResponse(srv_context.ot, response, message_info);

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

	return error;
}

otError coap_utils_modem_upload_measurement_response(otMessage *request_message,
													 const otMessageInfo *message_info,
													 otCoapCode code)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;

	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		goto end;
	}

	error = otCoapMessageInitResponse(response, request_message, OT_COAP_TYPE_ACKNOWLEDGMENT,
									  code);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendResponse(srv_context.ot, response, message_info);
end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

	return error;
}

static void handle_report_state_response(void *context, otMessage *message, const otMessageInfo *message_info, otError error)
{
    if (error != OT_ERROR_NONE)
    {
        LOG_ERR("report state request error %d: %s", error, otThreadErrorToString(error));
    } else if ((message_info != NULL) && (message != NULL)) {
		LOG_INF("report state response from ");
		LOG_HEXDUMP_INF(message_info->mPeerAddr.mFields.m8, sizeof(message_info->mPeerAddr.mFields.m8), "peer address:");
	}
}

static void handle_upload_measurement_response(void *context, otMessage *message, const otMessageInfo *message_info, otError error)
{
    if (error != OT_ERROR_NONE)
    {
        LOG_ERR("report state request error %d: %s", error, otThreadErrorToString(error));
    } else if ((message_info != NULL) && (message != NULL)) {
		LOG_INF("upload measurement response from ");
		LOG_HEXDUMP_INF(message_info->mPeerAddr.mFields.m8, sizeof(message_info->mPeerAddr.mFields.m8), "peer address:");
		if (otCoapMessageGetCode(message) == OT_COAP_CODE_CHANGED) {
			LOG_INF("Modem upload measurement success");
		} else if (otCoapMessageGetCode(message) == OT_COAP_CODE_SERVICE_UNAVAILABLE) {
			LOG_INF("Modem is busy, wait for next round");
		} else {
			LOG_ERR("Modem upload measurement failed");
		}
	}
}

otError coap_utils_modem_upload_measurement(const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *message;
	char uri[] = "modem";
	uint8_t modem_command = MODEM_COMMAND_UPLOAD_MEASUREMENT;

	message = otCoapNewMessage(srv_context.ot, NULL);
	if (message == NULL) {
		goto end;
	}

	otCoapMessageInit(message, OT_COAP_TYPE_CONFIRMABLE,
			  OT_COAP_CODE_PUT);
	otCoapMessageGenerateToken(message, OT_COAP_DEFAULT_TOKEN_LENGTH);
	error = otCoapMessageAppendUriPathOptions(message, uri);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(message);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otMessageAppend(message, &modem_command, sizeof(modem_command));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendRequest(srv_context.ot, message, message_info, &handle_upload_measurement_response, NULL);
	LOG_INF("Sent modem upload measurement");

end:
	if (error != OT_ERROR_NONE && message != NULL) {
		LOG_ERR("Failed to send modem upload measurement: %d", error);
		otMessageFree(message);
	}

	return error;
}

otError coap_utils_modem_report_state(const otMessageInfo *message_info,
					  uint8_t modem_state)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *message;
	char uri[] = "modem";
	uint8_t modem_command = MODEM_COMMAND_REPORT_STATE;

	message = otCoapNewMessage(srv_context.ot, NULL);
	if (message == NULL) {
		goto end;
	}

	otCoapMessageInit(message, OT_COAP_TYPE_CONFIRMABLE,
			  OT_COAP_CODE_PUT);
	otCoapMessageGenerateToken(message, OT_COAP_DEFAULT_TOKEN_LENGTH);
	error = otCoapMessageAppendUriPathOptions(message, uri);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(message);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otMessageAppend(message, &modem_command, sizeof(modem_command));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otMessageAppend(message, &modem_state, sizeof(modem_state));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendRequest(srv_context.ot, message, message_info, &handle_report_state_response, NULL);
	LOG_INF("Sent modem state: %d", modem_state);

end:
	if (error != OT_ERROR_NONE && message != NULL) {
		LOG_ERR("Failed to send modem state: %d", error);
		otMessageFree(message);
	}

	return error;
}

static void send_modem_discover_request(struct k_work *item)
{
	ARG_UNUSED(item);
	uint8_t command = (uint8_t)MODEM_COMMAND_DISCOVER;

	LOG_INF("Send 'discover' request");
	coap_send_request(COAP_METHOD_PUT,
			  (const struct sockaddr *)&multicast_local_addr,
			  modem_option, &command, sizeof(command), NULL);
}

static void on_thread_state_changed(otChangedFlags flags, struct openthread_context *ot_context,
				    void *user_data)
{
	if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_CHILD:
		case OT_DEVICE_ROLE_ROUTER:
		case OT_DEVICE_ROLE_LEADER:
			k_work_submit_to_queue(&coap_client_workq, &on_connect_work);
			is_connected = true;
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			k_work_submit_to_queue(&coap_client_workq, &on_disconnect_work);
			is_connected = false;
			break;
		}
	}
}
static struct openthread_state_changed_cb ot_state_chaged_cb = {
	.state_changed_cb = on_thread_state_changed
};

static void submit_work_if_connected(struct k_work *work)
{
	if (is_connected) {
		k_work_submit_to_queue(&coap_client_workq, work);
	} else {
		LOG_INF("Connection is broken");
	}
}

void coap_client_utils_init(ot_connection_cb_t on_connect,
			    ot_disconnection_cb_t on_disconnect)
{
	coap_init(AF_INET6, NULL);


	k_work_queue_init(&coap_client_workq);

	k_work_queue_start(&coap_client_workq, coap_client_workq_stack_area,
					K_THREAD_STACK_SIZEOF(coap_client_workq_stack_area),
					COAP_WORKQ_PRIORITY, NULL);

	k_work_init(&on_connect_work, on_connect);
	k_work_init(&on_disconnect_work, on_disconnect);
	k_work_init(&modem_discover_work, send_modem_discover_request);

	openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb);
	openthread_start(openthread_get_default_context());
}

void coap_utils_modem_discover(void)
{
	submit_work_if_connected(&modem_discover_work);
}

static void provisioning_request_handler(void *context, otMessage *message,
					 const otMessageInfo *message_info)
{
	otError error;
	otMessageInfo msg_info;

	ARG_UNUSED(context);
#if 0
	if (!srv_context.provisioning_enabled) {
		LOG_WRN("Received provisioning request but provisioning "
			"is disabled");
		return;
	}
#endif
	LOG_INF("Received provisioning request");

	if ((otCoapMessageGetType(message) == OT_COAP_TYPE_NON_CONFIRMABLE) &&
	    (otCoapMessageGetCode(message) == OT_COAP_CODE_GET)) {
		msg_info = *message_info;
		memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

		srv_context.on_provisioning_request();

		error = provisioning_response_send(message, &msg_info);
		if (error == OT_ERROR_NONE) {
			//srv_context.on_provisioning_request();
			srv_context.provisioning_enabled = false;
		}
	}
}

static void modem_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	static uint16_t message_id;

	if (otIp6IsAddressEqual(&(message_info->mPeerAddr), otThreadGetMeshLocalEid(srv_context.ot))) {
		LOG_WRN("Received message from itself");
		return;
	}

	if (otCoapMessageGetMessageId(message) == message_id) {
		LOG_WRN("Received the same message id");
		return;
	}
	message_id = otCoapMessageGetMessageId(message);

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Modem handler - Unexpected CoAP code");
		return;
	}

	srv_context.on_modem_request(message, message_info);
}

static void coap_default_handler(void *context, otMessage *message,
				 const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	ARG_UNUSED(message);
	ARG_UNUSED(message_info);

	LOG_INF("Received CoAP message that does not match any request "
		"or resource");
}

int ot_coap_init(provisioning_request_callback_t on_provisioning_request,
                 modem_request_callback_t on_modem_request)
{
	otError error;

	srv_context.provisioning_enabled = true;
	srv_context.on_provisioning_request = on_provisioning_request;
	srv_context.on_modem_request = on_modem_request;

	srv_context.ot = openthread_get_default_instance();
	if (!srv_context.ot) {
		LOG_ERR("There is no valid OpenThread instance");
		error = OT_ERROR_FAILED;
		goto end;
	}

	provisioning_resource.mContext = srv_context.ot;
	provisioning_resource.mHandler = provisioning_request_handler;

	modem_resource.mContext = srv_context.ot;
	modem_resource.mHandler = modem_request_handler;

	otCoapSetDefaultHandler(srv_context.ot, coap_default_handler, NULL);
	otCoapAddResource(srv_context.ot, &modem_resource);
	otCoapAddResource(srv_context.ot, &provisioning_resource);

	error = otCoapStart(srv_context.ot, COAP_PORT);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to start OT CoAP. Error: %d", error);
		goto end;
	}

end:
	return error == OT_ERROR_NONE ? 0 : 1;
}
