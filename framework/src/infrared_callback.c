/**
 * @file infrared_callback.c
 * @brief Direct per-instance callback dispatch for physical Capture and HWTIMER devices.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "infrared_private.h"

/**
 * @brief Deliver input readiness to the fixed logical device after verifying ownership.
 * @param index Configured registry index.
 * @param physical Originating physical device.
 * @param size Driver-specific sample indication count.
 * @return RT_EOK for active or stale indications.
 */
rt_err_t infrared_device_dispatch_rx(rt_size_t index, rt_device_t physical, rt_size_t size)
{
    struct infrared_device *ir = infrared_device_get(index);
    (void)size;
    if (ir && ir->rx.claimed_input == physical)
    {
        infrared_device_notify_rx(ir);
    }
    return RT_EOK;
}

/**
 * @brief Complete one output envelope interval for the fixed logical device.
 * @param index Configured registry index.
 * @param physical Originating envelope timer.
 * @param size Driver-provided completion count.
 * @return Backend result, or RT_EOK for a stale indication.
 */
rt_err_t infrared_device_dispatch_tx(rt_size_t index, rt_device_t physical, rt_size_t size)
{
    struct infrared_device *ir = infrared_device_get(index);
    if (ir && ir->tx.claimed_output == physical && ir->config.output.ops &&
        ir->config.output.ops->io.tx.indicate)
    {
        return ir->config.output.ops->io.tx.indicate(ir, size);
    }
    return RT_EOK;
}

/**
 * @brief Select a caller-supplied or generated fixed-index input callback.
 * @param ir Registered input device.
 * @return Fixed instance ISR trampoline, or NULL when not configured.
 */
infrared_indicate_fn infrared_device_rx_indicate(const struct infrared_device *ir)
{
#if IR_CFG_CHECK_PARAMETERS
    if (!ir || !ir->registered)
    {
        return RT_NULL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    return ir->config.input.indicate;
}

/**
 * @brief Select a caller-supplied or generated fixed-index output callback.
 * @param ir Registered transmitter.
 * @return Fixed instance ISR trampoline, or NULL when not configured.
 */
infrared_indicate_fn infrared_device_tx_indicate(const struct infrared_device *ir)
{
#if IR_CFG_CHECK_PARAMETERS
    if (!ir || !ir->registered)
    {
        return RT_NULL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    return ir->config.output.indicate;
}
