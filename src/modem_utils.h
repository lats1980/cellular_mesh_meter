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

#endif /* __MODEM_UTILS_H__ */