#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include "modem_utils.h"
#include <modem/modem_slm.h>

LOG_MODULE_REGISTER(modem_util, CONFIG_MODEM_UTILS_LOG_LEVEL);

/**@brief Enumeration describing mqtt cloud state. */
typedef enum {
	MQTT_CLOUD_STATE_DISCONNECTED,
	MQTT_CLOUD_STATE_CONNECTING,
	MQTT_CLOUD_STATE_CONNECTED
} mqtt_cloud_state;

/**@brief Enumeration describing mqtt publish state. */
typedef enum {
	MQTT_PUB_STATE_IDLE,
	MQTT_PUB_STATE_PUBLISHING,
	MQTT_PUB_STATE_FAILED
} mqtt_publish_state;

#define MODEM_WORKQ_STACK_SIZE 2048
#define MODEM_WORKQ_PRIORITY 5
#define MQTT_PUBLISH_CHECK_TIMEOUT K_SECONDS(10)
#define MQTT_PUBLISH_MAX_RETRY 3
#define MQTT_PUBLISH_BUFFER_SIZE 1024

#define SLM_SYNC_CHECK_TIMEOUT K_MSEC(CONFIG_MODEM_SLM_POWER_PIN_TIME + 1000)
#define SLM_SYNC_STR       "Ready\r\n"
/* TODO: Make modem link mode configurable */
#define SLM_LINK_MODE      "AT%XSYSTEMMODE=0,1,0,0\r\n"
#define SLM_LINK_CEREG_5   "AT+CEREG=5\r\n"
#define SLM_LINK_CFUN_1    "AT+CFUN=1\r\n"
/* TODO: Make MQTT cfg/con/pub arguments configurable */
#define SLM_MQTT_CFG       "AT#XMQTTCFG=\"MyMQTT-Client-ID-1234\",300,1\r\n"
#define SLM_MQTT_CON       "AT#XMQTTCON=1,\"\",\"\",\"broker.hivemq.com\",1883\r\n"
#define SLM_MQTT_PUB_A     "AT#XMQTTPUB=\"slm\",\""
#define SLM_MQTT_PUB_B     "\",1,0\r\n"

static modem_state current_modem_state = MODEM_STATE_UNKNOWN;
static mqtt_cloud_state mqtt_state = MQTT_CLOUD_STATE_DISCONNECTED;
static modem_utils_state_handler_t state_handler;
static mqtt_publish_state mqtt_pub_state = MQTT_PUB_STATE_IDLE;
static uint8_t mqtt_pub_retries = 0;
static char mqtt_publish_buffer[MQTT_PUBLISH_BUFFER_SIZE];

K_THREAD_STACK_DEFINE(modem_workq_stack_area, MODEM_WORKQ_STACK_SIZE);

SLM_MONITOR(network, "\r\n+CEREG:", cereg_mon);
SLM_MONITOR(mqtt_cloud, "\r\n#XMQTTEVT:", mqtt_cloud_mon);

static struct k_work_q modem_workq;
static struct k_work on_modem_sync_work;
static struct k_work publish_send_work;
static struct k_work_delayable modem_sync_check_work;
static struct k_work_delayable publish_check_work;

void modem_link_init(void);

static void cereg_mon(const char *notif)
{
	int status = atoi(notif + strlen("\r\n+CEREG: "));

	if (status == 1 || status == 5) {
		LOG_INF("LTE connected");
        modem_cloud_connect();
	} else {
        LOG_INF("LTE disconnected");
        mqtt_state = MQTT_CLOUD_STATE_DISCONNECTED;
        modem_set_state(MODEM_STATE_OFF);
    }
}

static void mqtt_cloud_mon(const char *notif)
{
	int event, result;

    event = atoi(notif + strlen("\r\n#XMQTTEVT: "));
    result = atoi(notif + strlen("\r\n#XMQTTEVT: 0,"));

	if (event == 0) {
        if (result == 0) {
		    LOG_INF("MQTT broker connected");
            mqtt_state = MQTT_CLOUD_STATE_CONNECTED;
            modem_set_state(MODEM_STATE_IDLE);
        } else {
            LOG_INF("MQTT broker disconnected");
        }
    } else if (event == 3) {
        if (result == 0) {
            LOG_INF("MQTT message published");
            mqtt_pub_state = MQTT_PUB_STATE_IDLE;
        } else {
            LOG_INF("MQTT message not published");
            mqtt_pub_state = MQTT_PUB_STATE_FAILED;
        }
        k_work_cancel_delayable(&publish_check_work);
    }
}

static void on_slm_data(const uint8_t *data, size_t datalen)
{
	LOG_HEXDUMP_INF(data, datalen, "SLM data received");
	if (current_modem_state == MODEM_STATE_UNKNOWN) {
		if (!strncmp((const char *)data, SLM_SYNC_STR, strlen(SLM_SYNC_STR))) {
			LOG_INF("Modem is synchronized");
            k_work_cancel_delayable(&modem_sync_check_work);
            k_work_submit_to_queue(&modem_workq, &on_modem_sync_work);
		}
	}
}

static void on_modem_sync(struct k_work *item)
{
	ARG_UNUSED(item);
    modem_link_init();
}

void publish_send(struct k_work *work)
{
    int ret;

    LOG_INF("Sending SLM data");
    ret = modem_slm_send_cmd(mqtt_publish_buffer, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM data (error: %d)", ret);
        return;
    }
    mqtt_pub_retries++;
}

static void modem_sync_check(struct k_work *work)
{
    if (current_modem_state == MODEM_STATE_UNKNOWN) {
        int ret;
        LOG_ERR("Modem not sync. Wake up SLM now");
        ret = modem_slm_power_pin_toggle();
        if (ret) {
            LOG_ERR("Cannot wake up SLM (error: %d)", ret);
        } else {
            k_work_schedule(&modem_sync_check_work, SLM_SYNC_CHECK_TIMEOUT);
        }
    }
}

static void publish_check(struct k_work *work)
{
    if (mqtt_pub_state == MQTT_PUB_STATE_PUBLISHING) {
        if (mqtt_pub_retries >= MQTT_PUBLISH_MAX_RETRY) {
            LOG_ERR("MQTT publish retries exceeded");
            mqtt_pub_state = MQTT_PUB_STATE_FAILED;
            return;
        }
        LOG_INF("MQTT publish still in progress. Resending...");
        k_work_submit_to_queue(&modem_workq, &publish_send_work);
    } else {
        LOG_INF("MQTT publish completed with state: %d", mqtt_pub_state);
    }
}

int modem_init(modem_utils_state_handler_t handler)
{
    int ret;

	k_work_queue_start(&modem_workq, modem_workq_stack_area,
					   K_THREAD_STACK_SIZEOF(modem_workq_stack_area),
					   MODEM_WORKQ_PRIORITY, NULL);

    k_work_init(&on_modem_sync_work, on_modem_sync);
    k_work_init(&publish_send_work, publish_send);
    k_work_init_delayable(&modem_sync_check_work, modem_sync_check);
    k_work_init_delayable(&publish_check_work, publish_check);

    state_handler = handler;
    state_handler(MODEM_STATE_UNKNOWN);

    ret = modem_slm_init(on_slm_data);
    if (ret) {
        LOG_ERR("Cannot initialize SLM (error: %d)", ret);
        return ret;
    }
    k_work_schedule(&modem_sync_check_work, K_NO_WAIT);

    return 0;
}

void modem_link_init(void)
{
    int ret;

    ret = modem_slm_send_cmd(SLM_LINK_MODE, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM command %s (error: %d)", SLM_LINK_MODE, ret);
    }
    ret = modem_slm_send_cmd(SLM_LINK_CEREG_5, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM command %s (error: %d)", SLM_LINK_CEREG_5, ret);
    }
    ret = modem_slm_send_cmd(SLM_LINK_CFUN_1, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM command %s (error: %d)", SLM_LINK_CFUN_1, ret);
    }
}

modem_state modem_get_state(void)
{
    return current_modem_state;
}

void modem_set_state(modem_state state)
{
    current_modem_state = state;
    state_handler(current_modem_state);
}

int modem_cloud_connect(void)
{
    int ret;

    if (mqtt_state != MQTT_CLOUD_STATE_DISCONNECTED) {
        return -EBUSY;
    }

    mqtt_state = MQTT_CLOUD_STATE_CONNECTING;
    ret = modem_slm_send_cmd(SLM_MQTT_CFG, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM command %s (error: %d)", SLM_MQTT_CFG, ret);
        return ret;
    }
    ret = modem_slm_send_cmd(SLM_MQTT_CON, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM command %s (error: %d)", SLM_MQTT_CON, ret);
        return ret;
    }

    return 0;
}

int modem_cloud_upload_data(const uint8_t *data, size_t size)
{
    if (!data) {
        LOG_ERR("Data is NULL");
        return -EINVAL;
    }
    if (MQTT_PUBLISH_BUFFER_SIZE < (strlen(SLM_MQTT_PUB_A) + size + strlen(SLM_MQTT_PUB_B))) {
        LOG_ERR("Data size exceeds buffer size");
        return -ENOMEM;
    }   

    if (mqtt_pub_state == MQTT_PUB_STATE_PUBLISHING) {
        LOG_WRN("MQTT publish in progress");
        return -EBUSY;
    } else if (mqtt_pub_state == MQTT_PUB_STATE_FAILED) {
        LOG_ERR("MQTT publish failed");
        mqtt_pub_state = MQTT_PUB_STATE_IDLE;
        return -EIO;
    }
    mqtt_pub_retries = 0;
    /* Create AT command to publish data */
    memset(mqtt_publish_buffer, 0, sizeof(mqtt_publish_buffer));
    memcpy(mqtt_publish_buffer, SLM_MQTT_PUB_A, strlen(SLM_MQTT_PUB_A));
    memcpy(mqtt_publish_buffer + strlen(SLM_MQTT_PUB_A), data, size);
    memcpy(mqtt_publish_buffer + strlen(SLM_MQTT_PUB_A) + size, SLM_MQTT_PUB_B, strlen(SLM_MQTT_PUB_B));
    k_work_submit_to_queue(&modem_workq, &publish_send_work);
    k_work_schedule(&publish_check_work, MQTT_PUBLISH_CHECK_TIMEOUT);
    mqtt_pub_state = MQTT_PUB_STATE_PUBLISHING;

    return 0;
}