#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modem_utils.h"

#include <modem/modem_slm.h>


LOG_MODULE_REGISTER(modem_util, CONFIG_MODEM_UTILS_LOG_LEVEL);

static modem_state current_modem_state = MODEM_STATE_OFF;
static modem_utils_state_handler_t state_handler;

#define MODEM_WORKQ_STACK_SIZE 2048
#define MODEM_WORKQ_PRIORITY 5

#define SLM_SYNC_STR    "Ready\r\n"

#define SLM_LINK_NBMODE    "AT%XSYSTEMMODE=0,1,0,0\r\n"
#define SLM_LINK_CEREG_5    "AT+CEREG=5\r\n"
#define SLM_LINK_CFUN_1    "AT+CFUN=1\r\n"

K_THREAD_STACK_DEFINE(modem_workq_stack_area, MODEM_WORKQ_STACK_SIZE);

static struct k_work_q modem_workq;

static struct k_work on_modem_sync_work;

void modem_link_init(void);

static void on_slm_data(const uint8_t *data, size_t datalen)
{
	LOG_HEXDUMP_INF(data, datalen, "SLM data received");
	if (current_modem_state == MODEM_STATE_OFF) {
		if (!strncmp((const char *)data, SLM_SYNC_STR, strlen(SLM_SYNC_STR))) {
			LOG_INF("Modem is ready");
			current_modem_state = MODEM_STATE_IDLE;
            state_handler(current_modem_state);
            k_work_submit_to_queue(&modem_workq, &on_modem_sync_work);
		}
	}
}

static void on_modem_sync(struct k_work *item)
{
	ARG_UNUSED(item);
    modem_link_init();
}

int modem_init(modem_utils_state_handler_t handler)
{
    int ret;

	k_work_queue_start(&modem_workq, modem_workq_stack_area,
					K_THREAD_STACK_SIZEOF(modem_workq_stack_area),
					MODEM_WORKQ_PRIORITY, NULL);

    k_work_init(&on_modem_sync_work, on_modem_sync);

    state_handler = handler;
    state_handler(current_modem_state);

    ret = modem_slm_init(on_slm_data);
    if (ret) {
        LOG_ERR("Cannot initialize SLM (error: %d)", ret);
        return ret;
    }

    return 0;
}

void modem_link_init(void)
{
    int ret;

    ret = modem_slm_send_cmd(SLM_LINK_NBMODE, 0);
    if (ret) {
        LOG_ERR("Cannot send SLM command %s (error: %d)", SLM_LINK_NBMODE, ret);
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