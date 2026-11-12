/**
 * @file infrared_device.h
 * @brief Public fixed-index multi-device infrared framework API.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * @attention The legacy API is built in a different mutually exclusive package mode.
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_DEVICE_H
#define INFRARED_DEVICE_H

#include <rtthread.h>
#include <rtdevice.h>
#include "infrared_backend.h"
#include "infrared_codec.h"
#include "infrared_framework_config.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/** @brief Opaque infrared instance owned by the framework's static registry. */
struct infrared_device;

/** @brief RT-Thread physical callback used by Capture and timer-backed transmitters. */
typedef rt_err_t (*infrared_indicate_fn)(rt_device_t physical, rt_size_t size);

/** @brief Input adapter and caller-owned immutable backend configuration. */
struct infrared_input_config
{
    const struct infrared_backend_ops *ops; /**< RX backend; RT_NULL disables RX. */
    const void *settings;                   /**< RX-specific constant configuration; valid for device lifetime. */
    infrared_indicate_fn indicate;          /**< Required fixed-instance callback for Input Capture; unused by PIN. */
};

/** @brief Output adapter and caller-owned immutable backend configuration. */
struct infrared_output_config
{
    const struct infrared_backend_ops *ops; /**< TX backend; RT_NULL disables TX. */
    const void *settings;                   /**< TX-specific constant configuration; valid for device lifetime. */
    /**
     * @brief TX completion wait timeout in RT-Thread ticks.
     * @note 0 and RT_WAITING_FOREVER (-1) request an indefinite wait; positive
     *       values request a finite wait. The caller must supply representable
     *       values and avoid other negative values; no range check is performed.
     */
    rt_int32_t timeout_ticks;
    infrared_indicate_fn indicate;          /**< Required fixed-instance callback for PWM envelope timer. */
};

/** @brief Protocol operations and optional application-owned parser context. */
struct infrared_codec_config
{
    const struct infrared_codec_ops *ops; /**< Protocol-specific encode/decode operations. */
    void *context;                        /**< Optional external context, otherwise ops->context_for_index resolves it. */
};

/** @brief Immutable registration specification; application selects a stable index. */
struct infrared_device_config
{
    rt_size_t index;                      /**< Sparse registry index: 0..INFRARED_MAX_DEVICES-1. */
    const char *name;                     /**< Permanent RT-Thread logical device name. */
    struct infrared_input_config input;   /**< Optional receive adapter. */
    struct infrared_output_config output; /**< Optional transmit adapter. */
    struct infrared_codec_config codec;   /**< One codec with per-instance state. */
};

#ifdef INFRARED_USING_DIAGNOSTICS
/** @brief Runtime diagnostic snapshot grouped by independent input/output directions. */
struct infrared_device_info
{
    const char *name; /**< Registered logical name. */
    rt_size_t index;  /**< Stable logical index. */
    rt_bool_t opened; /**< Logical device open state. */
    struct
    {
        const char *backend;     /**< Input implementation or "none". */
        rt_bool_t configured;    /**< Receive adapter present. */
        rt_size_t queued_frames; /**< Completed input frames pending. */
    } input;                     /**< Receiver diagnostics. */
    struct
    {
        const char *backend;  /**< Output implementation or "none". */
        rt_bool_t configured; /**< Transmit adapter present. */
    } output;                 /**< Transmitter diagnostics. */
};
#endif /* INFRARED_USING_DIAGNOSTICS */

/**
 * @brief Thread-context callback for a complete decoded infrared frame.
 * @param device Logical infrared receiver that decoded the frame.
 * @param frame Temporary decoded frame, valid only during the callback.
 * @param user_data Opaque application context registered for this device.
 * @return RT_EOK when handled, otherwise a negative RT-Thread error.
 * @note Runs synchronously in the caller of infrared_device_poll(), never in an ISR.
 *       A failure is returned from poll() but does not cause frame replay.
 */
typedef rt_err_t (*infrared_frame_callback_t)(struct infrared_device *device,
                                          const struct infrared_frame *frame,
                                          void *user_data);

/** @brief Callback for ISR-safe notification that raw receive data is ready to decode. */
typedef void (*infrared_rx_notify_t)(struct infrared_device *device, void *user_data);

/**
 * @brief Register one device in its explicit static registry index.
 * @param config Immutable name, hardware backend choices and protocol configuration.
 * @return RT_EOK or a negative RT-Thread error. No application storage is required.
 * @note Register at most once per index, before physical resource use; config.settings
 *       must refer to constant storage that remains valid until reboot.
 */
rt_err_t infrared_device_register(const struct infrared_device_config *config);

/**
 * @brief Resolve a registered device without searching or moving sparse slots.
 * @param index Zero-based index configured at registration.
 * @return Registered instance or RT_NULL for an invalid/empty slot.
 */
struct infrared_device *infrared_device_get(rt_size_t index);

/**
 * @brief Identify the codec selected at device registration (not runtime switchable).
 * @param device Registered logical device.
 * @return Selected protocol operation table or RT_NULL when invalid.
 */
const struct infrared_codec_ops *infrared_device_codec(struct infrared_device *device);

/** @brief Return the number of currently registered infrared device instances. */
rt_size_t infrared_device_get_count(void);

/**
 * @brief Obtain the embedded RT-Thread device object for standard device operations.
 * @param device Registered infrared instance.
 * @return Pointer to the RT-Thread device, or RT_NULL for an invalid device.
 */
rt_device_t infrared_device_rt_device(struct infrared_device *device);

/**
 * @brief Decode one available raw pulse in the caller thread.
 * @param device Open receiver with a single decoding owner.
 * @return RT_EOK after consuming one pulse, -RT_EEMPTY when idle, or a negative error.
 */
rt_err_t infrared_device_poll(struct infrared_device *device);

/**
 * @brief Start or stop an opened input adapter without tracking a redundant enable flag.
 * @param device Registered device.
 * @param enable RT_TRUE enables sampling; RT_FALSE stops and flushes partial state.
 * @return RT_EOK or a negative RT-Thread error.
 * @note The caller serializes start/stop against polling, callbacks and device close.
 */
rt_err_t infrared_device_receive_enable(struct infrared_device *device, rt_bool_t enable);

/**
 * @brief Send whole protocol frames; callers serialize TX and close operations.
 * @param device Open output instance.
 * @param frames Input array of complete protocol frames.
 * @param count Number of frames (not byte count).
 * @return Number of frames sent completely.
 */
rt_size_t infrared_device_write_frames(struct infrared_device *device,
                                       const struct infrared_frame *frames, rt_size_t count);

/**
 * @brief Read completed protocol frames from the per-device frame FIFO.
 * @param device Open receiver instance.
 * @param frames Destination array of complete frame records.
 * @param count Maximum number of frame records.
 * @return Number of records copied.
 */
rt_size_t infrared_device_read_frames(struct infrared_device *device,
                                      struct infrared_frame *frames, rt_size_t count);

/**
 * @brief Dispatch a caller-supplied fixed-index Capture callback to its receiver.
 * @param index Explicitly registered logical device index.
 * @param physical BSP capture device invoking the callback.
 * @param size Driver notification count.
 * @return RT_EOK (stale indications are ignored).
 */
rt_err_t infrared_device_dispatch_rx(rt_size_t index, rt_device_t physical, rt_size_t size);

/**
 * @brief Dispatch a caller-supplied fixed-index PWM envelope callback.
 * @param index Explicitly registered logical device index.
 * @param physical BSP envelope HWTIMER invoking the callback.
 * @param size Driver notification count.
 * @return Backend callback status or RT_EOK for stale indications.
 */
rt_err_t infrared_device_dispatch_tx(rt_size_t index, rt_device_t physical, rt_size_t size);

/**
 * @brief Install an ISR-safe receive-ready hook; callers coordinate its lifetime.
 * @param device Registered receiver instance.
 * @param notify Callback invoked after new samples, overflow or frame FIFO space; NULL disables.
 * @param user_data Application-owned callback argument, valid until the hook is cleared.
 * @return RT_EOK for a valid receiver, otherwise -RT_EINVAL when checks are enabled.
 * @note Caller must serialize hook replacement against live ISR callbacks; hook must not block.
 */
rt_err_t infrared_device_set_rx_notify(struct infrared_device *device,
                                       infrared_rx_notify_t notify, void *user_data);

/**
 * @brief Register a complete-frame callback and its device-local user context.
 * @param device Registered infrared receiver.
 * @param callback Handler invoked in the polling thread; NULL selects frame FIFO mode.
 * @param user_data Application-owned context, retained while callbacks may run.
 * @return RT_EOK, or -RT_EINVAL for an invalid receiver.
 * @note Can be called before or after open, including an auto-started NEC device.
 *       The caller must synchronize replacement against a running frame callback
 *       and keep the previous user_data alive until that callback completes.
 *       Each poll uses one consistent callback/context snapshot. Callback mode
 *       delivers new frames directly; any earlier FIFO records remain readable.
 */
rt_err_t infrared_device_set_frame_callback(struct infrared_device *device,
                                            infrared_frame_callback_t callback,
                                            void *user_data);

#ifdef INFRARED_USING_DIAGNOSTICS
/**
 * @brief Retrieve an interrupt-consistent status snapshot for diagnostics.
 * @param device Registered instance.
 * @param info Output diagnostic structure.
 * @return RT_EOK or -RT_EINVAL.
 */
rt_err_t infrared_device_get_info(struct infrared_device *device,
                                  struct infrared_device_info *info);
#endif /* INFRARED_USING_DIAGNOSTICS */

/** @brief Infrared-specific control commands for the RT-Thread logical device. */
enum infrared_control_cmd
{
    INFRARED_CTRL_RX_ENABLE = 0xA0, /**< Pointer to rt_bool_t: enable/disable RX. */
    INFRARED_CTRL_RX_FLUSH          /**< Pointer to rt_bool_t: TRUE clears immediately, FALSE at next poll. */
};

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* INFRARED_DEVICE_H */
