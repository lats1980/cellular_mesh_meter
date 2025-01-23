#ifndef __MODEM_UTILS_H__
#define __MODEM_UTILS_H__

/**@brief Enumeration describing modem state. */
typedef enum {
	MODEM_STATE_OFF,
	MODEM_STATE_IDLE,
	MODEM_STATE_BUSY
} modem_state;

typedef void (*modem_utils_state_handler_t)(modem_state state);

/**
 * @brief Initialize modem.
 */
int modem_init(modem_utils_state_handler_t handler);

/**
 * @brief Get modem state.
 */
modem_state modem_get_state(void);

/**
 * @brief Set modem state.
 */
void modem_set_state(modem_state);

/**
 * @brief Initialize modem cloud connection.
 */
int modem_cloud_connect(void);

int modem_cloud_upload_data(uint8_t *data, size_t size);

#endif /* __MODEM_UTILS_H__ */