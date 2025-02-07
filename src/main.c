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

#define DEFAULT_MEASURE_CNT 10
#define MEASURE_BLOCK_SIZE 512
#define UPLOAD_MEASUREMENT_TIMEOUT K_MSEC(100)

static bool uploading_measurement = false;
static struct k_work_delayable uploading_measurement_work;
static uint32_t max_block_count = DEFAULT_MEASURE_CNT;

#if CONFIG_BT_NUS

#define COMMAND_UPLOAD_MEASUREMENT  'u'
#define COMMAND_CHANGE_UPLOAD_COUNT 'c'

int upload_measurement(void);

static void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	LOG_INF("Received data: %c", data[0]);

	switch (*data) {
	case COMMAND_UPLOAD_MEASUREMENT:
		upload_measurement();
		break;
	
	case COMMAND_CHANGE_UPLOAD_COUNT:
		if (len > 1) {
			max_block_count = atoi((const char *)&data[1]);
		} else {
			LOG_WRN("Invalid data length");
		}
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
		upload_measurement();
	}

	if (buttons & DK_BTN2_MSK) {
	}

	if (buttons & DK_BTN3_MSK) {
	}

	if (buttons & DK_BTN4_MSK) {
	}
}

static void on_modem_request(otMessage *message, const otMessageInfo *message_info)
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
			coap_utils_send_response(message, message_info, OT_COAP_CODE_CHANGED);
		} else {
			LOG_INF("Modem is busy, wait for next round");
			coap_utils_send_response(message, message_info, OT_COAP_CODE_SERVICE_UNAVAILABLE);
		}
		break;

	default:
		break;
	}
}

static void on_meter_block_tx(void *context,
							  uint8_t *block,
							  uint32_t position,
							  uint16_t *block_length,
							  bool *more)
{
	static uint32_t block_count = 0;
	ARG_UNUSED(position);

	LOG_INF("send block: Num %i Len %i pos: %i", block_count, *block_length, position);
	/* Fill tx block with ascii 0 ~ 9 */
	for (uint16_t i = 0; i < *block_length; i++) {
		block[i] = 48 + i % 10;
	}
	LOG_HEXDUMP_INF(block, *block_length, "Sent block:");
	if (block_count == max_block_count - 1)
	{
		block_count = 0;
		*more     = false;
		uploading_measurement = false;
	}
	else
	{
		*more = true;
		block_count++;
	}
}

static otError on_meter_block_rx(void *context,
								 const uint8_t *block,
								 uint32_t position,
								 uint16_t block_length,
								 bool more,
								 uint32_t total_length)
{
	ARG_UNUSED(total_length);
	int ret;

	LOG_INF("received block: Num %i Len %i more: %d", position / block_length, block_length, more);
	LOG_HEXDUMP_INF(block, block_length, "Received block:");
	ret = modem_cloud_upload_data(block, (size_t)block_length);
	if (ret != 0) {
		if (ret == -EBUSY) {
			LOG_DBG("Modem is busy, wait for next round");
			return OT_ERROR_BUSY;
		} else if (ret == -ENOMEM) {
			LOG_ERR("No memory to upload data");
			return OT_ERROR_NO_BUFS;
		} else {
			LOG_ERR("Fail to upload data to cloud");
			return OT_ERROR_FAILED;
		}
	}
	if (more == false) {
		LOG_INF("Received all blocks");
		modem_set_state(MODEM_STATE_IDLE);
	}
	return OT_ERROR_NONE;
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

void uploading_measurement_handler(struct k_work *work)
{
	static uint32_t block_count = 0;

	if (uploading_measurement) {
		uint8_t block[MEASURE_BLOCK_SIZE];
		int ret;

		/* Fill tx block with ascii 0 ~ 9 */
		for (uint16_t i = 0; i < MEASURE_BLOCK_SIZE; i++) {
			block[i] = 48 + i % 10;
		}
		ret = modem_cloud_upload_data(block, MEASURE_BLOCK_SIZE);
		if (ret != 0) {
			if (ret == -EBUSY) {
				LOG_DBG("Modem is busy, wait for next round");
				k_work_schedule(&uploading_measurement_work, UPLOAD_MEASUREMENT_TIMEOUT);
			} else if (ret == -ENOMEM) {
				LOG_ERR("No memory to upload data");
				goto error;
			} else {
				LOG_ERR("Fail to upload data to cloud");
				goto error;
			}
		} else {
			LOG_INF("Sent block: Num %i Len %i", block_count, MEASURE_BLOCK_SIZE);
			if (block_count == max_block_count - 1) {
				block_count = 0;
				uploading_measurement = false;
				modem_set_state(MODEM_STATE_IDLE);
			} else {
				block_count++;
				k_work_schedule(&uploading_measurement_work, UPLOAD_MEASUREMENT_TIMEOUT);
			}
		}
	}
	return;
error:
	block_count = 0;
	uploading_measurement = false;
	modem_set_state(MODEM_STATE_IDLE);
	return;
}

int upload_measurement(void)
{
	if (uploading_measurement) {
		LOG_INF("Already uploading measurement");
		return -EBUSY;
	}

	if (modem_get_state() == MODEM_STATE_IDLE) {
		LOG_INF("Modem is idle, start uploading measurement");
		modem_set_state(MODEM_STATE_BUSY);
		uploading_measurement = true;
		k_work_schedule(&uploading_measurement_work, K_NO_WAIT);
	} else if (modem_get_state() == MODEM_STATE_BUSY) {
		LOG_INF("Modem is busy, wait for next round");
		return -EBUSY;
	} else {
		LOG_INF("Modem is off. Ask remote modem to upload measurement");
		coap_utils_modem_discover();
	}

	return 0;
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

	k_work_init_delayable(&uploading_measurement_work, uploading_measurement_handler);

	ret = ot_coap_init(&on_modem_request, &on_meter_block_tx, &on_meter_block_rx);
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
