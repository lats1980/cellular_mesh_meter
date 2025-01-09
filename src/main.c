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

#include "coap_meter_utils.h"

#if CONFIG_BT_NUS
#include "ble_utils.h"
#endif

#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(cellular_mesh_meter, CONFIG_CELLULAR_MESH_METER_LOG_LEVEL);

#define OT_CONNECTION_LED DK_LED1
#define BLE_CONNECTION_LED DK_LED2
#define MTD_SED_LED DK_LED3
#define LIGHT_LED DK_LED4

#if CONFIG_BT_NUS

#define COMMAND_REQUEST_UNICAST 'u'
#define COMMAND_REQUEST_MULTICAST 'm'
#define COMMAND_REQUEST_PROVISIONING 'p'

/**@brief Enumeration describing modem state. */
typedef enum {
	CELLULAR_MODEM_STATE_OFF,
	CELLULAR_MODEM_STATE_IDLE,
	CELLULAR_MODEM_STATE_BUSY
} cellular_modem_state;

static cellular_modem_state modem_state = CELLULAR_MODEM_STATE_OFF;

static void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	LOG_INF("Received data: %c", data[0]);

	switch (*data) {
	case COMMAND_REQUEST_UNICAST:
		//coap_client_toggle_one_light();
		break;

	case COMMAND_REQUEST_MULTICAST:
		//coap_client_toggle_mesh_lights();
		break;

	case COMMAND_REQUEST_PROVISIONING:
		coap_client_send_modem_discover_request();
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

static void on_mtd_mode_toggle(uint32_t med)
{
#if IS_ENABLED(CONFIG_PM_DEVICE)
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(cons)) {
		return;
	}

	if (med) {
		pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
	} else {
		pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	}
#endif
	dk_set_led(MTD_SED_LED, med);
}

static void on_button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;

	if (buttons & DK_BTN1_MSK) {
		/* test code to change modem state
		*/
		if (modem_state == CELLULAR_MODEM_STATE_OFF) {
			modem_state = CELLULAR_MODEM_STATE_IDLE;
			dk_set_led_on(LIGHT_LED);
			dk_set_led_on(MTD_SED_LED);
		} else if (modem_state == CELLULAR_MODEM_STATE_IDLE) {
			modem_state = CELLULAR_MODEM_STATE_BUSY;
			dk_set_led_on(LIGHT_LED);
			dk_set_led_off(MTD_SED_LED);
		} else {
			modem_state = CELLULAR_MODEM_STATE_OFF;
			dk_set_led_off(LIGHT_LED);
			dk_set_led_off(MTD_SED_LED);
		}
		LOG_INF("Modem state: %d", modem_state);
	}

	if (buttons & DK_BTN2_MSK) {
		//coap_client_toggle_mesh_lights();
	}

	if (buttons & DK_BTN3_MSK) {
		coap_client_toggle_minimal_sleepy_end_device();
	}

	if (buttons & DK_BTN4_MSK) {
		coap_client_send_modem_discover_request();
	}
}

static void on_modem_request(otMessage *message,
				  const otMessageInfo *message_info)
{
	uint8_t command;

	if (otMessageRead(message, otMessageGetOffset(message), &command, 1) != 1) {
		LOG_ERR("Modem handler - Missing modem command");
	}

	char string[OT_IP6_ADDRESS_STRING_SIZE], string2[OT_IP6_ADDRESS_STRING_SIZE];

	otIp6AddressToString(&(message_info->mPeerAddr), string, sizeof(string));
	otIp6AddressToString(&(message_info->mSockAddr), string2, sizeof(string2));

	LOG_HEXDUMP_INF(string, strlen(string), "coap request from ");
	LOG_HEXDUMP_INF(string2, strlen(string2), "coap request to ");
	LOG_INF("Got command: %d", command);

	switch (command) {
	case THREAD_COAP_UTILS_MODEM_CMD_DISCOVER:
		if ((modem_state == CELLULAR_MODEM_STATE_IDLE ) || (modem_state == CELLULAR_MODEM_STATE_BUSY)) {
			otMessageInfo update_state_message_info;
			memset(&update_state_message_info, 0, sizeof(update_state_message_info));
			update_state_message_info.mPeerAddr = message_info->mPeerAddr;
			update_state_message_info.mPeerPort = COAP_PORT;
			coap_client_send_modem_update_state(&update_state_message_info, modem_state);
		} else {	//CELLULAR_MODEM_STATE_OFF
			LOG_INF("Modem is off");
		}
		break;

	case THREAD_COAP_UTILS_MODEM_CMD_UPDATE_STATE_IDLE:
	case THREAD_COAP_UTILS_MODEM_CMD_UPDATE_STATE_BUSY:
		coap_server_send_modem_update_state_response(message, message_info);
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

	coap_client_utils_init(on_ot_connect, on_ot_disconnect, on_mtd_mode_toggle);

	return 0;
}
