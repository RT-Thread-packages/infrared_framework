/**
 * @file infrared_capture.h
 * @brief RT-Thread Input Capture backend configuration and private state.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_CAPTURE_H
#define INFRARED_CAPTURE_H

#include <rtdevice.h>
#include "infrared_backend.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/** @brief BSP-created Input Capture device parameters. */
struct infrared_capture_config
{
    const char *device_name; /**< RT-Thread Input Capture device name. */
    rt_base_t active_level;  /**< Electrical level representing a MARK interval. */
    rt_size_t watermark;     /**< Receive notification threshold; zero means one. */
};

/** @brief Per-device Input Capture state; the BSP owns its sample FIFO. */
struct infrared_capture_state
{
    rt_device_t device;      /**< BSP Input Capture device handle. */
    rt_bool_t opened; /**< Whether the capture device has an active physical open. */
};

/** @brief Selectable Input Capture receive implementation. */
extern const struct infrared_backend_ops infrared_capture_ops;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INFRARED_CAPTURE_H */
