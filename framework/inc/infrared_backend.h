/**
 * @file infrared_backend.h
 * @brief Protocol-neutral infrared input/output hardware adapter contract.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * @attention A backend's direction determines which union member is active.
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_BACKEND_H
#define INFRARED_BACKEND_H

#include <rtthread.h>
#include "infrared_codec.h"

struct infrared_device;

/**
 * @brief Shared transmit buffer contract at the beginning of adapter-owned user_data.
 * @note The backend owns the storage and remaining private ISR state. Calls to write,
 *       stop and close must be serialized by the application.
 */
struct infrared_tx_buffer
{
    infrared_pulse_t *pulses; /**< Stable encoded storage until the backend is stopped. */
    rt_size_t capacity;       /**< Maximum number of pulse entries. */
    volatile rt_size_t count; /**< Committed pulse count visible to the timer ISR. */
    volatile rt_err_t result; /**< Timer ISR outcome returned to the waiting sender. */
};

/** @brief Hardware adapter direction; one instance may hold one adapter per direction. */
enum infrared_backend_direction
{
    INFRARED_BACKEND_INPUT = 1, /**< RX: acquire packed pulses. */
    INFRARED_BACKEND_OUTPUT = 2 /**< TX: transmit a pre-encoded envelope. */
};

/**
 * @brief Shared lifecycle operations with direction-specific IO signatures.
 * @note Never call io.rx members through a TX table or io.tx through an RX table.
 */
struct infrared_backend_ops
{
    const char *name;                                  /**< Backend diagnostic label; not a registered RT-Thread device name. */
    enum infrared_backend_direction direction;         /**< Active union branch and valid adapter role. */
    rt_err_t (*open)(struct infrared_device *device);  /**< Acquire physical resources. */
    rt_err_t (*start)(struct infrared_device *device); /**< Start sampling or transmission. */
    rt_err_t (*stop)(struct infrared_device *device);  /**< Stop physical activity. */
    /** @brief Try hardware teardown once; report failure without auto-retrying. Retain ISR storage if still reachable. */
    rt_err_t (*close)(struct infrared_device *device);
    union
    {
        struct
        {
            rt_size_t (*read)(struct infrared_device *device, infrared_pulse_t *pulses,
                              rt_size_t capacity);             /**< Read complete pulse records without waiting. */
            rt_err_t (*flush)(struct infrared_device *device); /**< Return RT_EOK only when raw samples are cleared. */
        } rx;                                                  /**< Valid only for INFRARED_BACKEND_INPUT. */
        struct
        {
            /** Process one HWTIMER completion interrupt for the selected transmitter. */
            rt_err_t (*indicate)(struct infrared_device *device, rt_size_t count);
        } tx; /**< Valid only for INFRARED_BACKEND_OUTPUT. */
    } io;     /**< Direction-tagged callback union; never casts incompatible functions. */
};

#endif /* INFRARED_BACKEND_H */
