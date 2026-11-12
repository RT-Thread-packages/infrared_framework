/**
 * @file infrared_capture.c
 * @brief RT-Thread Input Capture pulse acquisition adapter.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * @attention Hardware resources are provided by RT-Thread devices registered by the BSP.
 *
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#include "infrared_private.h"
#include "infrared_capture.h"

#define DBG_TAG "infrared.capture"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static rt_err_t infrared_capture_stop(struct infrared_device *ir);

/**
 * @brief Locate the BSP-owned capture device by RT-Thread name.
 * @param ir Infrared receiver using the Input Capture adapter.
 * @return RT_EOK or -RT_ERROR if the physical device does not exist.
 */
static rt_err_t infrared_capture_open(struct infrared_device *ir)
{
    const struct infrared_capture_config *cfg =
        (const struct infrared_capture_config *)ir->config.input.settings;
    ir->rx.backend.capture.device = rt_device_find(cfg->device_name);
    if (!ir->rx.backend.capture.device)
    {
        return -RT_ERROR;
    }
    return infrared_device_bind_physical(ir, ir->rx.backend.capture.device, IR_RESOURCE_INPUT);
}

/**
 * @brief Open physical capture and configure an RX notification watermark.
 * @param ir Infrared receiver using the BSP capture ringbuffer.
 * @return RT_EOK or an RT-Thread device error.
 * @note On failure the caller owns the single best-effort close attempt.
 */
static rt_err_t infrared_capture_start(struct infrared_device *ir)
{
    const struct infrared_capture_config *cfg =
        (const struct infrared_capture_config *)ir->config.input.settings;
    rt_err_t ret;
    rt_size_t watermark = cfg->watermark;
    if (ir->rx.backend.capture.opened)
    {
        LOG_E("%s: capture already open", ir->config.name);
        return -RT_EBUSY;
    }
    ret = rt_device_open(ir->rx.backend.capture.device, RT_DEVICE_OFLAG_RDONLY);
    if (ret != RT_EOK)
    {
        LOG_E("%s: capture physical open failed (%d)", ir->config.name, ret);
        return ret;
    }
    ir->rx.backend.capture.opened = RT_TRUE;
    if (!watermark)
    {
        watermark = 1;
    }
    ret = rt_device_control(ir->rx.backend.capture.device,
                            INPUTCAPTURE_CMD_SET_WATERMARK, &watermark);
    if (ret == RT_EOK && !infrared_device_rx_indicate(ir))
    {
        ret = -RT_EINVAL;
    }
    if (ret == RT_EOK)
    {
        ret = rt_device_set_rx_indicate(ir->rx.backend.capture.device,
                                        infrared_device_rx_indicate(ir));
    }
    if (ret != RT_EOK)
    {
        /* The caller performs the one close attempt; start does not retry it. */
        LOG_E("%s: capture start failed (%d)", ir->config.name, ret);
    }
    else
    {
        LOG_I("%s: capture input started", ir->config.name);
    }
    return ret;
}

/**
 * @brief Stop capture and release the driver-owned ringbuffer after caller stops polling.
 * @param ir Infrared receiver whose capture device is stopped.
 * @return RT_EOK or the physical close error.
 * @note Callback removal and device close are both attempted once.
 */
static rt_err_t infrared_capture_stop(struct infrared_device *ir)
{
    rt_err_t ret;
    rt_err_t close_ret; /* Separate physical close result. */
    if (!ir->rx.backend.capture.opened)
    {
        return RT_EOK;
    }
    ret = rt_device_set_rx_indicate(ir->rx.backend.capture.device, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_E("%s: capture callback detach failed (%d)", ir->config.name, ret);
    }
    close_ret = rt_device_close(ir->rx.backend.capture.device);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: capture physical close failed (%d)", ir->config.name, close_ret);
    }
    else
    {
        ir->rx.backend.capture.opened = RT_FALSE;
    }
    return ret != RT_EOK ? ret : close_ret;
}

/**
 * @brief Read one sample from RT-Thread's existing capture ringbuffer.
 * @param ir Infrared receiver bound to an open capture device.
 * @param pulses Destination array of packed 32-bit pulse segments.
 * @param capacity Number of destination pulse entries.
 * @return Number of complete normalized samples read.
 */
static rt_size_t infrared_capture_read(struct infrared_device *ir,
                                       infrared_pulse_t *pulses, rt_size_t capacity)
{
    const struct infrared_capture_config *cfg =
        (const struct infrared_capture_config *)ir->config.input.settings;
    struct rt_inputcapture_data sample; /* One driver pulse for one decoding call. */
    rt_size_t count;
    rt_bool_t active_high = (cfg->active_level != 0);
    if (!ir->rx.backend.capture.opened || !capacity)
    {
        return 0;
    }
    count = rt_device_read(ir->rx.backend.capture.device, 0, &sample, 1);
#if IR_CFG_CHECK_DEVICE
    if (count > 1)
    {
        infrared_report_overflow(ir);
        LOG_E("%s: Input Capture returned too many pulses", ir->config.name);
        return 0;
    }
#endif /* IR_CFG_CHECK_DEVICE */
    if (count)
    {
        pulses[0] = INFRARED_PULSE_PACK(
            (sample.is_high == active_high) ? INFRARED_MARK : INFRARED_SPACE,
            sample.pulsewidth_us);
    }
    return count;
}

/**
 * @brief Discard unread Input Capture samples in the BSP-owned ringbuffer.
 * @param ir Infrared receiver bound to an open capture device.
 * @return RT_EOK on success, -RT_ENOSYS if unsupported, or a BSP control error.
 */
static rt_err_t infrared_capture_flush(struct infrared_device *ir)
{
    if (!ir->rx.backend.capture.opened)
    {
        return RT_EOK;
    }
    /* This BSP command must really clear pending samples for synchronous RX_FLUSH. */
#ifdef INPUTCAPTURE_CMD_CLEAR_BUF
    return rt_device_control(ir->rx.backend.capture.device, INPUTCAPTURE_CMD_CLEAR_BUF, RT_NULL);
#else
    LOG_W("%s: capture buffer-clear control unavailable", ir->config.name);
    return -RT_ENOSYS;
#endif /* INPUTCAPTURE_CMD_CLEAR_BUF */
}

/**
 * @brief Clear the capture binding without deregistering its BSP device.
 * @param ir Infrared receiver whose capture device was stopped.
 * @return RT_EOK or the physical capture stop/close error.
 * @note Failed teardown leaves the claim in place to prevent unsafe reuse.
 */
static rt_err_t infrared_capture_close(struct infrared_device *ir)
{
    rt_err_t ret = RT_EOK; /* Outcome of the single physical stop attempt. */
    if (ir->rx.backend.capture.opened)
    {
        ret = infrared_capture_stop(ir);
    }
    if (ret == RT_EOK)
    {
        (void)infrared_device_bind_physical(ir, RT_NULL, IR_RESOURCE_INPUT);
        ir->rx.backend.capture.device = RT_NULL;
    }
    /* A failed physical close is an error for the caller, not a retry stage. */
    return ret;
}

/**
 * @brief Immutable Input Capture adapter selected by infrared_device_register.
 */
const struct infrared_backend_ops infrared_capture_ops = {
    .name = "capture",
    .direction = INFRARED_BACKEND_INPUT,
    .open = infrared_capture_open,
    .start = infrared_capture_start,
    .stop = infrared_capture_stop,
    .close = infrared_capture_close,
    .io.rx = { infrared_capture_read, infrared_capture_flush }
};
