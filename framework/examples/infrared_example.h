/**
 * @file infrared_example.h
 * @brief Optional Kconfig-selected infrared device registration and worker API.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-11-01
 *
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-11-01     wdfk-prog         first version
 */
#ifndef INFRARED_EXAMPLE_H
#define INFRARED_EXAMPLE_H

#include "infrared_device.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * @brief Assign an enabled example slot's codec and decoded-frame consumer.
 * @param index Kconfig example slot index.
 * @param codec Concrete NEC or application codec operations.
 * @param context Optional caller-owned parser context (NULL uses context_for_index).
 * @param callback Complete-frame callback, or NULL to retain the normal frame FIFO.
 * @param user_data Per-device application context for callback.
 * @return RT_EOK or -RT_EINVAL/-RT_EBUSY for invalid/already started selection.
 * @note Configure CUSTOM slots before infrared_example_start(); NEC slots are
 *       automatically bound during example component initialization. The
 *       callback runs in the receive worker, not in the capture ISR.
 */
rt_err_t infrared_example_configure(rt_size_t index,
                                    const struct infrared_codec_ops *codec, void *context,
                                    infrared_frame_callback_t callback, void *user_data);

/**
 * @brief Register configured example devices and optionally start their shared worker.
 * @return RT_EOK or the first RT-Thread registration/open/worker error.
 * @note Each example slot independently selects NEC or CUSTOM. All-NEC setups
 *       start automatically; CUSTOM slots must be configured explicitly by the
 *       application before startup, including mixed NEC/CUSTOM setups.
 */
rt_err_t infrared_example_start(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* INFRARED_EXAMPLE_H */
