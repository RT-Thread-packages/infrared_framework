/**
 * @file infrared_pin.c
 * @brief PIN/HWTIMER pulse acquisition through the RT-Thread device layer.
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
#include "infrared_pin.h"

#define DBG_TAG "infrared.pin"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/**
 * @brief Period of the PIN timestamp clock in microseconds, used for modulo-wrap conversion.
 * @note The STM32F103CB profile sets 60000 us so 100-kHz TIM2 fits its 16-bit ARR.
 */
#ifndef INFRARED_PIN_TIMER_PERIOD_US
#define INFRARED_PIN_TIMER_PERIOD_US 1000000U
#endif /* INFRARED_PIN_TIMER_PERIOD_US */

static rt_err_t infrared_pin_close(struct infrared_device *ir);

#if IR_CFG_PIN_POOL_ENABLED
/** @brief Compact ISR FIFO storage for selected PIN receiver capacity. */
static rt_uint8_t infrared_pin_storage[IR_CONFIGURED_PIN_COUNT]
                                      [INFRARED_PIN_PULSE_CAPACITY * sizeof(infrared_pulse_t)];
/** @brief Current owner markers for open PIN receivers. */
static rt_bool_t infrared_pin_used[IR_CONFIGURED_PIN_COUNT];
#endif /* IR_CFG_PIN_POOL_ENABLED */

/**
 * @brief Reserve a PIN ISR FIFO from the compact configured pool.
 * @param ir Device acquiring one PIN receiver.
 * @return RT_EOK or -RT_ENOMEM if all configured FIFO slots are used.
 */
static rt_err_t infrared_pin_acquire(struct infrared_device *ir)
{
#if IR_CFG_PIN_POOL_ENABLED
    rt_size_t index;                             /* Candidate GPIO input pool slot. */
    rt_base_t level = rt_hw_interrupt_disable(); /* Protect pool ownership. */
    for (index = 0; index < IR_CONFIGURED_PIN_COUNT; index++)
    {
        if (!infrared_pin_used[index])
        {
            infrared_pin_used[index] = RT_TRUE;
            ir->rx.backend.pin.storage = infrared_pin_storage[index];
            ir->rx.backend.pin.pool_index = index;
            rt_hw_interrupt_enable(level);
            return RT_EOK;
        }
    }
    rt_hw_interrupt_enable(level);
#endif /* IR_CFG_PIN_POOL_ENABLED */
    return -RT_ENOMEM;
}

/**
 * @brief Release the PIN FIFO only after its IRQ is detached.
 * @param ir Device that owns the FIFO.
 */
static void infrared_pin_release(struct infrared_device *ir)
{
#if IR_CFG_PIN_POOL_ENABLED
    if (ir->rx.backend.pin.storage)
    {
        rt_base_t level = rt_hw_interrupt_disable(); /* Protect pool ownership. */
        infrared_pin_used[ir->rx.backend.pin.pool_index] = RT_FALSE;
        ir->rx.backend.pin.storage = RT_NULL;
        rt_hw_interrupt_enable(level);
    }
#else
    (void)ir;
#endif /* IR_CFG_PIN_POOL_ENABLED */
}

/**
 * @brief Capture a completed electrical interval in GPIO interrupt context.
 * @param parameter Infrared device passed as the pin IRQ callback argument.
 */
static void infrared_pin_irq(void *parameter)
{
    struct infrared_device *ir = (struct infrared_device *)parameter;
    const struct infrared_pin_config *cfg =
        (const struct infrared_pin_config *)ir->config.input.settings;
    struct rt_hwtimerval stamp;
    rt_uint32_t now;
    rt_uint32_t interval;
    rt_uint32_t level;
    infrared_pulse_t packed;

    if (rt_device_read(ir->rx.backend.pin.timer, 0, &stamp, sizeof(stamp)) != sizeof(stamp))
    {
        goto overflow;
    }
    now = (rt_uint32_t)((rt_uint64_t)(rt_uint32_t)stamp.sec * 1000000ULL + (rt_uint32_t)stamp.usec);
    level = (rt_uint32_t)rt_pin_read(cfg->pin);
    if (!ir->rx.backend.pin.last_valid)
    {
        ir->rx.backend.pin.last_valid = RT_TRUE;
        ir->rx.backend.pin.last_us = now;
        return;
    }
    /* Some BSPs expose timestamps only within the configured HWTIMER period.
     * For this board, the 60-ms modulo clock fits the 16-bit TIM2 counter;
     * a 59900 -> 100 us timestamp transition represents only 200 us.
     * Without wrap adjustment, unsigned subtraction would produce a false
     * overflow and discard an otherwise valid MR frame. BSPs that report
     * cumulative timestamps still use ordinary subtraction. Two or more
     * unobserved physical rollovers cannot be distinguished by this backend. */
    if (now < ir->rx.backend.pin.last_us &&
        ir->rx.backend.pin.last_us <= INFRARED_PIN_TIMER_PERIOD_US &&
        now < INFRARED_PIN_TIMER_PERIOD_US)
    {
        interval = INFRARED_PIN_TIMER_PERIOD_US - ir->rx.backend.pin.last_us + now;
    }
    else
    {
        interval = now - ir->rx.backend.pin.last_us;
    }
    ir->rx.backend.pin.last_us = now;
    if (!interval || interval > INFRARED_PULSE_MAX_US)
    {
        goto overflow;
    }
    packed = INFRARED_PULSE_PACK(level != (rt_uint32_t)cfg->active_level ? INFRARED_MARK : INFRARED_SPACE, interval);
    if (rt_ringbuffer_space_len(&ir->rx.backend.pin.queue) < sizeof(packed) ||
        rt_ringbuffer_put(&ir->rx.backend.pin.queue, (const rt_uint8_t *)&packed,
                          sizeof(packed)) != sizeof(packed))
    {
        goto overflow;
    }
    infrared_device_notify_rx(ir);
    return;
overflow:
    infrared_report_overflow(ir);
}

/**
 * @brief Allocate or attach one PIN ringbuffer and acquire its free-running HWTIMER.
 * @param ir Infrared device with GPIO and HWTIMER configuration.
 * @return RT_EOK or a negative RT-Thread error.
 * @note The original open error is returned; rollback failures are logged separately.
 */
static rt_err_t infrared_pin_open(struct infrared_device *ir)
{
    rt_err_t ret;
    rt_err_t close_ret; /* One physical rollback result, not a retry state. */
    const struct infrared_pin_config *cfg =
        (const struct infrared_pin_config *)ir->config.input.settings;
    rt_uint32_t frequency = cfg->timer_hz;
    rt_hwtimer_mode_t mode = HWTIMER_MODE_PERIOD;
    rt_size_t bytes = INFRARED_PIN_PULSE_CAPACITY * sizeof(infrared_pulse_t);

    ret = infrared_pin_acquire(ir);
    if (ret != RT_EOK)
    {
        LOG_E("%s: PIN FIFO pool exhausted (%d)", ir->config.name, ret);
        return ret;
    }
#if IR_CFG_CHECK_CONFIG
    if (bytes > 0x7FFFU || bytes < 2 * sizeof(infrared_pulse_t))
    {
        ret = -RT_EINVAL;
        goto fail_queue;
    }
#endif /* IR_CFG_CHECK_CONFIG */
    rt_ringbuffer_init(&ir->rx.backend.pin.queue, ir->rx.backend.pin.storage, (rt_int16_t)bytes);
    ir->rx.backend.pin.timer = rt_device_find(cfg->timer_name);
    if (!ir->rx.backend.pin.timer)
    {
        ret = -RT_ERROR;
        goto fail_queue;
    }
    ret = infrared_device_bind_physical(ir, ir->rx.backend.pin.timer, IR_RESOURCE_INPUT);
    if (ret != RT_EOK)
    {
        goto fail_queue;
    }
    ret = rt_device_open(ir->rx.backend.pin.timer, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK)
    {
        goto fail_bound;
    }
    if (!frequency)
    {
        frequency = 100000U;
    }
    ret = rt_device_control(ir->rx.backend.pin.timer, HWTIMER_CTRL_FREQ_SET, &frequency);
    if (ret != RT_EOK)
    {
        goto fail_timer;
    }
    ret = rt_device_control(ir->rx.backend.pin.timer, HWTIMER_CTRL_MODE_SET, &mode);
    if (ret != RT_EOK)
    {
        goto fail_timer;
    }
    rt_pin_mode(cfg->pin, PIN_MODE_INPUT_PULLUP);
    ret = rt_pin_attach_irq(cfg->pin, PIN_IRQ_MODE_RISING_FALLING, infrared_pin_irq, ir);
    if (ret != RT_EOK)
    {
        goto fail_timer;
    }
    ret = rt_pin_irq_enable(cfg->pin, PIN_IRQ_DISABLE);
    if (ret != RT_EOK)
    {
        goto fail_irq;
    }
    LOG_I("%s: PIN input opened", ir->config.name);
    return RT_EOK;

fail_irq:
    close_ret = rt_pin_detach_irq(cfg->pin);
    if (close_ret != RT_EOK)
    {
        /* The IRQ may still access the FIFO: leave its static storage claimed. */
        LOG_E("%s: PIN IRQ rollback failed (%d)", ir->config.name, close_ret);
        goto fail_retained;
    }
fail_timer:
    close_ret = rt_device_close(ir->rx.backend.pin.timer);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: PIN timer rollback failed (%d)", ir->config.name, close_ret);
        goto fail_retained;
    }
fail_bound:
    (void)infrared_device_bind_physical(ir, RT_NULL, IR_RESOURCE_INPUT);
fail_queue:
    ir->rx.backend.pin.timer = RT_NULL;
    infrared_pin_release(ir);
fail_retained:
    LOG_E("%s: PIN open failed (%d)", ir->config.name, ret);
    return ret;
}

/**
 * @brief Start the timestamp epoch before allowing GPIO edge callbacks.
 * @param ir Device owning this PIN and HWTIMER.
 * @return RT_EOK or an RT-Thread driver error.
 */
static rt_err_t infrared_pin_start(struct infrared_device *ir)
{
    const struct infrared_pin_config *cfg =
        (const struct infrared_pin_config *)ir->config.input.settings;
    rt_hwtimerval_t period;
    rt_err_t ret;
    period.sec = INFRARED_PIN_TIMER_PERIOD_US / 1000000U;
    period.usec = INFRARED_PIN_TIMER_PERIOD_US % 1000000U;
    if (rt_device_write(ir->rx.backend.pin.timer, 0, &period, sizeof(period)) != sizeof(period))
    {
        LOG_E("%s: PIN timer start failed", ir->config.name);
        return -RT_EIO;
    }
    ir->rx.backend.pin.last_valid = RT_FALSE;
    ir->rx.backend.pin.last_us = 0;
    ret = rt_pin_irq_enable(cfg->pin, PIN_IRQ_ENABLE);
    if (ret != RT_EOK)
    {
        LOG_E("%s: PIN IRQ enable failed (%d)", ir->config.name, ret);
    }
    return ret;
}

/**
 * @brief Disable GPIO edge capture before a stop/flush or close.
 * @param ir Device owning this PIN and HWTIMER.
 * @return RT_EOK or the first driver error.
 */
static rt_err_t infrared_pin_stop(struct infrared_device *ir)
{
    const struct infrared_pin_config *cfg =
        (const struct infrared_pin_config *)ir->config.input.settings;
    rt_err_t ret = rt_pin_irq_enable(cfg->pin, PIN_IRQ_DISABLE);
    rt_err_t timer_ret = rt_device_control(ir->rx.backend.pin.timer, HWTIMER_CTRL_STOP, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_E("%s: PIN IRQ disable failed (%d)", ir->config.name, ret);
    }
    if (timer_ret != RT_EOK)
    {
        LOG_E("%s: PIN timer stop failed (%d)", ir->config.name, timer_ret);
    }
    ir->rx.backend.pin.last_valid = RT_FALSE;
    return ret == RT_EOK ? timer_ret : ret;
}

/**
 * @brief Read complete 32-bit pulse entries from the PIN ISR-owned ringbuffer.
 * @param ir Infrared device with a valid PIN ringbuffer.
 * @param pulses Output array for packed pulse samples.
 * @param capacity Maximum number of pulse entries.
 * @return Number of whole pulse entries read without blocking.
 */
static rt_size_t infrared_pin_read(struct infrared_device *ir,
                                   infrared_pulse_t *pulses, rt_size_t capacity)
{
    rt_size_t bytes;
    rt_base_t level = rt_hw_interrupt_disable();
    bytes = rt_ringbuffer_get(&ir->rx.backend.pin.queue, (rt_uint8_t *)pulses,
                              (rt_uint16_t)(capacity * sizeof(infrared_pulse_t)));
    rt_hw_interrupt_enable(level);
    return bytes / sizeof(infrared_pulse_t);
}

/**
 * @brief Flush PIN input after a lost pulse or explicit reset.
 * @param ir Infrared device with a valid PIN ringbuffer.
 * @return RT_EOK after clearing the raw pulse ringbuffer.
 */
static rt_err_t infrared_pin_flush(struct infrared_device *ir)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_ringbuffer_reset(&ir->rx.backend.pin.queue);
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

/**
 * @brief Detach the PIN IRQ, close its HWTIMER and release the static PIN FIFO slot.
 * @param ir Infrared device owning this input adapter.
 * @return RT_EOK or the first physical IRQ/timer teardown error.
 * @note The callback must be detached before its static FIFO can be reused.
 */
static rt_err_t infrared_pin_close(struct infrared_device *ir)
{
    const struct infrared_pin_config *cfg =
        (const struct infrared_pin_config *)ir->config.input.settings;
    rt_err_t ret;
    rt_err_t close_ret; /* Driver result for each physical shutdown operation. */
    if (!ir->rx.claimed_input)
    {
        return RT_EOK;
    }
    /* A single best-effort teardown. Never reuse an ISR FIFO if IRQ detach fails. */
    ret = rt_pin_irq_enable(cfg->pin, PIN_IRQ_DISABLE);
    if (ret != RT_EOK)
    {
        LOG_E("%s: PIN IRQ disable failed (%d)", ir->config.name, ret);
    }
    close_ret = rt_pin_detach_irq(cfg->pin);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: PIN IRQ detach failed (%d)", ir->config.name, close_ret);
        /* The FIFO and timer remain claimed, but attempt to stop the timer once. */
        rt_err_t stop_ret = rt_device_control(ir->rx.backend.pin.timer, HWTIMER_CTRL_STOP, RT_NULL); /* Final stop attempt. */
        if (stop_ret != RT_EOK)
        {
            LOG_E("%s: PIN timer stop failed (%d)", ir->config.name, stop_ret);
        }
        return ret == RT_EOK ? close_ret : ret;
    }
    close_ret = rt_device_control(ir->rx.backend.pin.timer, HWTIMER_CTRL_STOP, RT_NULL);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: PIN timer stop failed (%d)", ir->config.name, close_ret);
        if (ret == RT_EOK)
        {
            ret = close_ret;
        }
    }
    close_ret = rt_device_close(ir->rx.backend.pin.timer);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: PIN timer close failed (%d)", ir->config.name, close_ret);
        return ret == RT_EOK ? close_ret : ret;
    }
    (void)infrared_device_bind_physical(ir, RT_NULL, IR_RESOURCE_INPUT);
    ir->rx.backend.pin.timer = RT_NULL;
    if (ir->rx.backend.pin.storage)
    {
        rt_ringbuffer_reset(&ir->rx.backend.pin.queue);
    }
    infrared_pin_release(ir);
    return ret;
}

/**
 * @brief Immutable PIN+HWTIMER adapter selected by the single infrared device registration.
 */
const struct infrared_backend_ops infrared_pin_ops = {
    .name = "pin",
    .direction = INFRARED_BACKEND_INPUT,
    .open = infrared_pin_open,
    .start = infrared_pin_start,
    .stop = infrared_pin_stop,
    .close = infrared_pin_close,
    .io.rx = { infrared_pin_read, infrared_pin_flush }
};
