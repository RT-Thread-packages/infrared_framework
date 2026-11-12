/**
 * @file infrared_private.h
 * @brief Internal infrared instance layout and physical callback ownership.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_PRIVATE_H
#define INFRARED_PRIVATE_H

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "ipc/ringbuffer.h"
#include "infrared_device.h"
#if IR_CFG_PIN_INPUT
#include "infrared_pin.h"
#endif /* IR_CFG_PIN_INPUT */
#if IR_CFG_CAPTURE_INPUT
#include "infrared_capture.h"
#endif /* IR_CFG_CAPTURE_INPUT */
#if IR_CFG_PWM_OUTPUT
#include "infrared_pwm.h"
#endif /* IR_CFG_PWM_OUTPUT */

/** @brief Direction of an exclusive physical callback-producing resource. */
enum infrared_resource_route
{
    IR_RESOURCE_INPUT = 0, /**< RX Input Capture/PIN hardware timer. */
    IR_RESOURCE_OUTPUT     /**< TX envelope hardware timer. */
};

/** @brief Interrupt-safe receive queue and optional input backend private state. */
struct infrared_rx_state
{
    struct rt_ringbuffer frame_queue; /**< Completed frame FIFO for receivers. */
    rt_uint8_t *frame_storage;        /**< Open-lifetime storage claimed from the RX frame pool. */
    rt_size_t frame_pool_index;       /**< RX frame pool allocation index. */
    rt_device_t claimed_input;        /**< Exclusively owned input timer or capture handle. */
    infrared_frame_callback_t frame_callback; /**< Optional thread-only complete-frame consumer. */
    void *frame_user_data;                   /**< Per-device context owned by the application. */
    infrared_rx_notify_t notify;      /**< Optional ISR-safe application wakeup hook. */
    void *notify_data;                /**< Lifetime controlled by application around open/close. */
    volatile rt_bool_t overflow;      /**< Lost sample: reset codec on next poll. */
    volatile rt_bool_t flush_pending; /**< Explicit deferred full RX flush at next poll. */
    union
    {
        rt_uint8_t unused; /**< No input backend selected. */
#if IR_CFG_PIN_INPUT
        struct infrared_pin_state pin; /**< Active when PIN is selected. */
#endif                                 /* IR_CFG_PIN_INPUT */
#if IR_CFG_CAPTURE_INPUT
        struct infrared_capture_state capture; /**< Active when Capture is selected. */
#endif                                         /* IR_CFG_CAPTURE_INPUT */
    } backend;                                 /**< Input alternatives share storage within each instance. */
};

/** @brief Shared TX completion and resource claims, with adapter-owned state attached. */
struct infrared_tx_state
{
    struct rt_completion done;             /**< Timer ISR notifies the sole waiting sender. */
    void *user_data;                       /**< Backend-owned state beginning with infrared_tx_buffer. */
    rt_device_t claimed_output;            /**< Exclusive envelope HWTIMER resource and operational handle. */
    rt_device_t claimed_channel;           /**< Shared PWM device; channel ownership is exclusive. */
    rt_int32_t claimed_channel_index;      /**< Channel number of claimed_channel. */
    rt_uint32_t claimed_channel_period_ns; /**< Shared carrier period in nanoseconds. */
};

/** @brief Single logical device; all storage belongs to the framework, not the example. */
struct infrared_device
{
    struct rt_device parent;              /**< First member required for RT-Thread device callbacks. */
    struct infrared_device_config config; /**< Immutable configuration copied at registration. */
    rt_size_t registry_index;             /**< Permanent index used by hardware callback trampolines. */
    struct infrared_rx_state rx;          /**< Independent receive state. */
    struct infrared_tx_state tx;          /**< Independent transmit state. */
    rt_bool_t registered;                 /**< Permanent slot identity published to IRQ callbacks. */
    rt_bool_t opened;                     /**< Physical devices acquired. */
};

/**
 * @brief Exclusively claim or release one physical callback-producing device.
 * @param ir Logical owner.
 * @param physical BSP device, or RT_NULL for release.
 * @param route Select input or output callback resource.
 * @return RT_EOK, or -RT_EBUSY on conflicting ownership.
 */
rt_err_t infrared_device_bind_physical(struct infrared_device *ir, rt_device_t physical,
                                       enum infrared_resource_route route);

/**
 * @brief Claim or release one PWM device/channel combination, without a second registry.
 * @param ir Logical owner.
 * @param physical PWM device handle, or RT_NULL to release.
 * @param channel Hardware channel ID.
 * @param period_ns Carrier period; all channels of one PWM timer must agree.
 * @return RT_EOK or -RT_EBUSY on a conflicting channel/frequency claim.
 */
rt_err_t infrared_device_bind_channel(struct infrared_device *ir, rt_device_t physical,
                                      rt_int32_t channel, rt_uint32_t period_ns);

/**
 * @brief Return input callback associated with the owning fixed index.
 * @param ir Registered device.
 * @return Stable RX callback pointer.
 */
infrared_indicate_fn infrared_device_rx_indicate(const struct infrared_device *ir);

/**
 * @brief Return output callback associated with the owning fixed index.
 * @param ir Registered device.
 * @return Stable TX callback pointer.
 */
infrared_indicate_fn infrared_device_tx_indicate(const struct infrared_device *ir);

/**
 * @brief Notify the receive worker after a raw sample was lost.
 * @param ir Receiver experiencing an interrupted pulse stream.
 */
void infrared_report_overflow(struct infrared_device *ir);

/**
 * @brief Invoke the configured ISR-safe RX notification callback.
 * @param ir Registered input instance receiving samples or frame queue capacity.
 */
void infrared_device_notify_rx(struct infrared_device *ir);

#endif /* INFRARED_PRIVATE_H */
