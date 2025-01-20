/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>
#include <ram_pwrdn.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>

#include "coap_utils.h"
#include "modem_utils.h"

#if CONFIG_BT_NUS
#include "ble_utils.h"
#endif

#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(cellular_mesh_meter, CONFIG_CELLULAR_MESH_METER_LOG_LEVEL);

#define OT_CONNECTION_LED	DK_LED1
#define BLE_CONNECTION_LED	DK_LED2
#define MODEM_IDLE_LED		DK_LED3
#define MODEM_BUSY_LED		DK_LED4

static bool uploading_measurement = false;

#if CONFIG_BT_NUS

#define COMMAND_REQUEST_UNICAST 'u'
#define COMMAND_REQUEST_MULTICAST 'm'
#define COMMAND_REQUEST_PROVISIONING 'p'

static void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	LOG_INF("Received data: %c", data[0]);

	switch (*data) {
	case COMMAND_REQUEST_UNICAST:
		break;

	case COMMAND_REQUEST_MULTICAST:
		break;

	case COMMAND_REQUEST_PROVISIONING:
		break;

	default:
		LOG_WRN("Received invalid data from NUS");
	}
}

static void on_ble_connect(struct k_work *item)
{
	ARG_UNUSED(item);

	dk_set_led_on(BLE_CONNECTION_LED);
}

static void on_ble_disconnect(struct k_work *item)
{
	ARG_UNUSED(item);

	dk_set_led_off(BLE_CONNECTION_LED);
}

#endif /* CONFIG_BT_NUS */

static void on_ot_connect(struct k_work *item)
{
	ARG_UNUSED(item);

	dk_set_led_on(OT_CONNECTION_LED);
}

static void on_ot_disconnect(struct k_work *item)
{
	ARG_UNUSED(item);

	dk_set_led_off(OT_CONNECTION_LED);
}

static void on_button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;

	if (buttons & DK_BTN1_MSK) {
		//TODO: reset uploading_measurement flag when uploading is done or timeout
		uploading_measurement = false;
		coap_utils_modem_discover();
	}

	if (buttons & DK_BTN2_MSK) {
	}

	if (buttons & DK_BTN3_MSK) {
	}

	if (buttons & DK_BTN4_MSK) {
	}
}

static void on_modem_request(otMessage *message,
				  const otMessageInfo *message_info)
{
	uint8_t command;
	modem_state current_modem_state = MODEM_STATE_OFF, remote_modem_state = MODEM_STATE_OFF;

	current_modem_state = modem_get_state();

	if (otMessageRead(message, otMessageGetOffset(message), &command,
		sizeof(command)) != sizeof(command)) {
		LOG_ERR("Modem handler - Missing modem command");
	}

	char ip_addr_from[OT_IP6_ADDRESS_STRING_SIZE], ip_addr_to[OT_IP6_ADDRESS_STRING_SIZE];

	otIp6AddressToString(&(message_info->mPeerAddr), ip_addr_from, sizeof(ip_addr_from));
	otIp6AddressToString(&(message_info->mSockAddr), ip_addr_to, sizeof(ip_addr_to));

	LOG_HEXDUMP_INF(ip_addr_from, strlen(ip_addr_from), "coap request from ");
	LOG_HEXDUMP_INF(ip_addr_to, strlen(ip_addr_to), "coap request to ");
	LOG_INF("Got command: %d", command);

	switch (command) {
	case MODEM_COMMAND_DISCOVER:
		if ((current_modem_state == MODEM_STATE_IDLE ) || (current_modem_state == MODEM_STATE_BUSY)) {
			otMessageInfo report_state_message_info;
			memset(&report_state_message_info, 0, sizeof(report_state_message_info));
			report_state_message_info.mPeerAddr = message_info->mPeerAddr;
			report_state_message_info.mPeerPort = COAP_PORT;
			coap_utils_modem_report_state(&report_state_message_info, current_modem_state);
		} else {	//MODEM_STATE_OFF
			LOG_INF("Modem is off");
		}
		break;

	case MODEM_COMMAND_REPORT_STATE:
		if (otMessageRead(message, otMessageGetOffset(message) + sizeof(command),
			&remote_modem_state, sizeof(remote_modem_state)) != sizeof(remote_modem_state)) {
			LOG_ERR("Missing modem state of the remote modem");
		} else {
			LOG_INF("Remote modem state: %d", remote_modem_state);
			coap_utils_modem_report_state_response(message, message_info);
			if ((remote_modem_state == MODEM_STATE_IDLE) && (uploading_measurement == false)) {
				//send modem upload measurement
				otMessageInfo upload_measurement_message_info;

				uploading_measurement = true;
				memset(&upload_measurement_message_info, 0, sizeof(upload_measurement_message_info));
				upload_measurement_message_info.mPeerAddr = message_info->mPeerAddr;
				upload_measurement_message_info.mPeerPort = COAP_PORT;
				coap_utils_modem_upload_measurement(&upload_measurement_message_info);
			}
		}
		break;

	case MODEM_COMMAND_UPLOAD_MEASUREMENT:
		LOG_INF("Receive Upload Measurement command");
		if (current_modem_state == MODEM_STATE_IDLE) {
			LOG_INF("Modem is idle, start uploading measurement");
			modem_set_state(MODEM_STATE_BUSY);
			coap_utils_modem_upload_measurement_response(message, message_info, OT_COAP_CODE_CHANGED);
		} else {
			LOG_INF("Modem is busy, wait for next round");
			coap_utils_modem_upload_measurement_response(message, message_info, OT_COAP_CODE_SERVICE_UNAVAILABLE);
		}
		break;

	default:
		break;
	}
}

static void on_modem_state_change(modem_state state)
{
	dk_set_led_off(MODEM_IDLE_LED);
	dk_set_led_off(MODEM_BUSY_LED);

	switch (state) {
	case MODEM_STATE_IDLE:
		dk_set_led_on(MODEM_IDLE_LED);
		break;

	case MODEM_STATE_BUSY:
		dk_set_led_on(MODEM_BUSY_LED);
		break;
	default:
		break;
	}
}

static void deactivate_provisionig(void)
{
	LOG_INF("Provisioning deactivated");
}

int main(void)
{
	int ret;

	LOG_INF("Start Cellular Mesh Meter sample");

	if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
		power_down_unused_ram();
	}

	ret = dk_buttons_init(on_button_changed);
	if (ret) {
		LOG_ERR("Cannot init buttons (error: %d)", ret);
		return 0;
	}

	ret = dk_leds_init();
	if (ret) {
		LOG_ERR("Cannot init leds, (error: %d)", ret);
		return 0;
	}

#if CONFIG_BT_NUS
	struct bt_nus_cb nus_clbs = {
		.received = on_nus_received,
		.sent = NULL,
	};

	ret = ble_utils_init(&nus_clbs, on_ble_connect, on_ble_disconnect);
	if (ret) {
		LOG_ERR("Cannot init BLE utilities");
		return 0;
	}

#endif /* CONFIG_BT_NUS */

#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_shell_uart), zephyr_cdc_acm_uart)
	const struct device *dev;
	uint32_t dtr = 0U;

	ret = usb_enable(NULL);
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (dev == NULL) {
		LOG_ERR("Failed to find specific UART device");
		return 0;
	}

	LOG_INF("Waiting for host to be ready to communicate");

	/* Data Terminal Ready - check if host is ready to communicate */
	while (!dtr) {
		ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		if (ret) {
			LOG_ERR("Failed to get Data Terminal Ready line state: %d",
				ret);
			continue;
		}
		k_msleep(100);
	}

	/* Data Carrier Detect Modem - mark connection as established */
	(void)uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
	/* Data Set Ready - the NCP SoC is ready to communicate */
	(void)uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);
#endif

	ret = ot_coap_init(&deactivate_provisionig, &on_modem_request);
	if (ret) {
		LOG_ERR("Could not initialize OpenThread CoAP");
	}

	coap_client_utils_init(on_ot_connect, on_ot_disconnect);

	ret = modem_init(on_modem_state_change);
	if (ret) {
		LOG_ERR("Cannot init modem (error: %d)", ret);
		return ret;
	}

	return 0;
}
