#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modem_utils.h"

#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(modem_util, CONFIG_MODEM_UTILS_LOG_LEVEL);

static modem_state current_modem_state = MODEM_STATE_UNKNOWN;
static modem_utils_state_handler_t state_handler;

int modem_init(modem_utils_state_handler_t handler)
{
    state_handler = handler;
    state_handler(MODEM_STATE_OFF);

    return 0;
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
    return 0;
}

int modem_cloud_upload_data(const uint8_t *data, size_t size)
{
    if (data) {
        LOG_HEXDUMP_INF(data, size, "upload data:");
    }
    return 0;
}

static int cmd_state(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
        if (current_modem_state == MODEM_STATE_OFF) {
            shell_fprintf(shell, SHELL_INFO, "current state: off\n");
        } else if (current_modem_state == MODEM_STATE_IDLE) {
            shell_fprintf(shell, SHELL_INFO, "current state: idle\n");
        } else if (current_modem_state == MODEM_STATE_BUSY) {
            shell_fprintf(shell, SHELL_INFO, "current state: busy\n");
        }
	} else {
        if (strcmp(argv[1], "off") == 0) {
            current_modem_state = MODEM_STATE_OFF;
        } else if (strcmp(argv[1], "idle") == 0) {
            current_modem_state = MODEM_STATE_IDLE;
        } else if (strcmp(argv[1], "busy") == 0) {
            current_modem_state = MODEM_STATE_BUSY;
        } else {
            shell_fprintf(shell, SHELL_INFO, "Invalid state\n");
            return -EINVAL;
        }
        state_handler(current_modem_state);
    }
    shell_fprintf(shell, SHELL_INFO, "Done\n");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_modem_utils,
	SHELL_CMD_ARG(
		state, NULL,
		"Get/Set modem state. (off, idle, busy)\n",
		cmd_state, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(modem_utils, &sub_modem_utils, "modem utils commands", NULL);