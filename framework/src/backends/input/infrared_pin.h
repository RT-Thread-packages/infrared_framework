/**
 * @file infrared_pin.h
 * @brief PIN input backend configuration and private per-device state.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_PIN_H
#define INFRARED_PIN_H

#include <rtdevice.h>
#include "ipc/ringbuffer.h"
#include "infrared_backend.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/** @brief Immutable GPIO-edge receive hardware configuration. */
struct infrared_pin_config
{
    const char *timer_name; /**< BSP HWTIMER used to timestamp pin edges. */
    rt_base_t pin;          /**< RT-Thread pin number. */
    rt_base_t active_level; /**< GPIO level identifying an optical MARK. */
    rt_uint32_t timer_hz;   /**< Hardware timer tick frequency, zero uses the default. */
};

/** @brief Runtime state held in the mutually exclusive receive backend union. */
struct infrared_pin_state
{
    rt_device_t timer;          /**< Free-running HWTIMER handle. */
    struct rt_ringbuffer queue; /**< FIFO metadata; storage is owned by this backend. */
    rt_uint8_t *storage;        /**< Open-lifetime ISR FIFO from the compact PIN pool. */
    rt_size_t pool_index;       /**< Claimed PIN buffer slot. */
    rt_uint32_t last_us;        /**< Previous GPIO edge timestamp in microseconds. */
    rt_bool_t last_valid;       /**< Whether last_us can be used. */
};

/** @brief Selectable PIN/HWTIMER receive implementation. */
extern const struct infrared_backend_ops infrared_pin_ops;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INFRARED_PIN_H */
