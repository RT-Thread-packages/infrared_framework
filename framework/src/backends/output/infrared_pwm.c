/**
 * @file infrared_pwm.c
 * @brief PWM carrier and HWTIMER envelope transmitter adapter.
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
#include "infrared_pwm.h"

#define DBG_TAG "infrared.pwm"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/**
 * @brief PWM-specific state and pulse memory, attached through the TX user_data pointer.
 * @note transfer is the first member so the framework can access its shared buffer contract.
 */
struct infrared_pwm_state
{
    struct infrared_tx_buffer transfer;                   /**< Framework-facing encoded transfer. */
    volatile rt_size_t index;                             /**< Next pulse for the HWTIMER ISR. */
    rt_bool_t used;                                       /**< Pool reservation ownership. */
    infrared_pulse_t storage[INFRARED_TX_PULSE_CAPACITY]; /**< Encoded pulses until stop. */
};

#if IR_CFG_PWM_POOL_ENABLED
/** @brief Compact PWM backend states, one per configured transmitter capacity. */
static struct infrared_pwm_state infrared_pwm_pool[IR_CONFIGURED_TX_COUNT];
#endif /* IR_CFG_PWM_POOL_ENABLED */

/**
 * @brief Reserve a stable envelope buffer for one active PWM transmitter.
 * @param ir Device acquiring the output pulse buffer.
 * @return RT_EOK or -RT_ENOMEM when no configured PWM buffer is available.
 */
static rt_err_t infrared_pwm_acquire(struct infrared_device *ir)
{
#if IR_CFG_PWM_POOL_ENABLED
    rt_size_t index;                             /* Candidate PWM backend state slot. */
    rt_base_t level = rt_hw_interrupt_disable(); /* Protect pool ownership. */
    for (index = 0; index < IR_CONFIGURED_TX_COUNT; index++)
    {
        struct infrared_pwm_state *state = &infrared_pwm_pool[index]; /* Available backend state. */
        if (!state->used)
        {
            state->used = RT_TRUE;
            state->transfer.pulses = state->storage;
            state->transfer.capacity = INFRARED_TX_PULSE_CAPACITY;
            state->transfer.count = 0;
            state->transfer.result = RT_EOK;
            state->index = 0;
            ir->tx.user_data = state;
            rt_hw_interrupt_enable(level);
            return RT_EOK;
        }
    }
    rt_hw_interrupt_enable(level);
#endif /* IR_CFG_PWM_POOL_ENABLED */
    return -RT_ENOMEM;
}

/**
 * @brief Free the envelope storage after stopping the physical timer.
 * @param ir Output instance holding the pool allocation.
 */
static void infrared_pwm_release(struct infrared_device *ir)
{
#if IR_CFG_PWM_POOL_ENABLED
    if (ir->tx.user_data)
    {
        struct infrared_pwm_state *state = (struct infrared_pwm_state *)ir->tx.user_data;
        rt_base_t level = rt_hw_interrupt_disable(); /* Release the backend-owned slot. */ /* Protect pool ownership. */
        state->transfer.count = 0;
        ir->tx.user_data = RT_NULL;
        state->used = RT_FALSE;
        rt_hw_interrupt_enable(level);
    }
#else
    (void)ir;
#endif /* IR_CFG_PWM_POOL_ENABLED */
}

/**
 * @brief Original transmitter's 500-us stabilization interval before the first envelope segment.
 */
#define INFRARED_TX_START_DELAY_US 500U
/**
 * @brief 1-MHz HWTIMER frequency: one timer tick per microsecond envelope duration.
 */
#define INFRARED_TX_TIMER_HZ 1000000U
/**
 * @brief Nominal 38-kHz carrier (1e9/38,000 = 26,316 ns rounded).
 */
#define INFRARED_PWM_PERIOD_NS 26316U
/**
 * @brief Original 1/3-duty active carrier interval (26,316/3 = 8,772 ns, legacy setting 8,770).
 */
#define INFRARED_PWM_ACTIVE_NS 8770U

/**
 * @brief Access the PWM device through its channel reservation.
 * @param ir Transmitter owning the numbered channel.
 * @return BSP PWM device associated with the active reservation.
 */
static struct rt_device_pwm *infrared_pwm_carrier(struct infrared_device *ir)
{
    return (struct rt_device_pwm *)ir->tx.claimed_channel;
}

/**
 * @brief Arm the next one-shot envelope interval.
 * @param ir Transmitter owning the HWTIMER.
 * @param duration_us Full pulse duration in microseconds.
 * @return RT_EOK or -RT_EIO if the timer could not be armed.
 */
static rt_err_t infrared_pwm_arm(struct infrared_device *ir, rt_uint32_t duration_us)
{
    rt_hwtimerval_t interval;
    interval.sec = duration_us / 1000000U;
    interval.usec = duration_us % 1000000U;
    if (rt_device_write(ir->tx.claimed_output, 0, &interval, sizeof(interval)) != sizeof(interval))
    {
        return -RT_EIO;
    }
    return RT_EOK;
}

/**
 * @brief Advance exactly one MARK/SPACE segment in the output timer ISR.
 * @param ir Transmitter resolved by the framework's fixed callback index.
 * @param size Unused callback count.
 * @return RT_EOK after updating the output or notifying its waiting sender.
 */
static rt_err_t infrared_pwm_timeout(struct infrared_device *ir, rt_size_t size)
{
    const struct infrared_pwm_config *cfg;
    struct infrared_pwm_state *state; /* Adapter-owned ISR cursor and transfer. */
    infrared_pulse_t pulse;
    rt_err_t ret = RT_EOK;
    rt_err_t stop_ret; /* Final carrier stop must not mask an earlier failure. */
    (void)size;
#if IR_CFG_CHECK_PARAMETERS
    if (!ir)
    {
        return RT_EOK;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    state = (struct infrared_pwm_state *)ir->tx.user_data;
    if (!state || !state->transfer.count)
    {
        return RT_EOK;
    }
    cfg = (const struct infrared_pwm_config *)ir->config.output.settings;
    ret = rt_pwm_disable(infrared_pwm_carrier(ir), cfg->pwm_channel);
    if (ret != RT_EOK)
    {
        goto complete;
    }
    if (state->index >= state->transfer.count)
    {
        goto complete;
    }
    pulse = state->transfer.pulses[state->index++];
    if (INFRARED_PULSE_LEVEL(pulse) == INFRARED_MARK)
    {
        ret = rt_pwm_enable(infrared_pwm_carrier(ir), cfg->pwm_channel);
    }
    if (ret == RT_EOK)
    {
        ret = infrared_pwm_arm(ir, INFRARED_PULSE_DURATION(pulse));
    }
    if (ret == RT_EOK)
    {
        return RT_EOK;
    }
complete:
    /* Even if the final stop succeeds, an earlier carrier stop error must reach the sender. */
    stop_ret = rt_pwm_disable(infrared_pwm_carrier(ir), cfg->pwm_channel);
    if (ret == RT_EOK)
    {
        ret = stop_ret;
    }
    state->transfer.count = 0;
    state->transfer.result = ret;
    rt_completion_done(&ir->tx.done);
    return RT_EOK;
}

/**
 * @brief Acquire and configure RT-Thread PWM and HWTIMER resources.
 * @param ir Infrared device providing channel, carrier and timer configuration.
 * @return RT_EOK or the first configuration error; rollback is attempted only once.
 */
static rt_err_t infrared_pwm_open(struct infrared_device *ir)
{
    const struct infrared_pwm_config *cfg =
        (const struct infrared_pwm_config *)ir->config.output.settings;
    rt_err_t ret;
    rt_err_t close_ret; /* A single rollback attempt, never a reference-recovery retry. */
    rt_err_t detach_ret; /* Callback removal is independent of physical close. */
    rt_uint32_t frequency = INFRARED_TX_TIMER_HZ;
    rt_hwtimer_mode_t mode = HWTIMER_MODE_ONESHOT;
    rt_uint32_t period = cfg->period_ns;
    rt_uint32_t active = cfg->pulse_ns;
    struct rt_device_pwm *pwm; /* Carrier device before claiming its channel. */
    rt_device_t timer;         /* Envelope timer before claiming its physical handle. */

    ret = infrared_pwm_acquire(ir);
    if (ret != RT_EOK)
    {
        LOG_E("%s: PWM pulse pool exhausted (%d)", ir->config.name, ret);
        return ret;
    }
    if (!period)
    {
        period = INFRARED_PWM_PERIOD_NS;
    }
    if (!active)
    {
        active = INFRARED_PWM_ACTIVE_NS;
    }
    pwm = (struct rt_device_pwm *)rt_device_find(cfg->pwm_name);
    if (!pwm)
    {
        ret = -RT_ERROR;
        goto fail_pool;
    }
    ret = infrared_device_bind_channel(ir, (rt_device_t)pwm, cfg->pwm_channel, period);
    if (ret != RT_EOK)
    {
        goto fail_pool;
    }
    ret = rt_pwm_set(pwm, cfg->pwm_channel, period, active);
    if (ret != RT_EOK)
    {
        goto fail_set;
    }
    ret = rt_pwm_disable(pwm, cfg->pwm_channel);
    if (ret != RT_EOK)
    {
        /* A failed disable cannot prove the carrier stopped: do not reassign
         * this hardware channel, and do not retry in the framework. */
        goto fail_retained;
    }
    timer = rt_device_find(cfg->timer_name);
    if (!timer)
    {
        ret = -RT_ERROR;
        goto fail_channel;
    }
    ret = infrared_device_bind_physical(ir, timer, IR_RESOURCE_OUTPUT);
    if (ret != RT_EOK)
    {
        goto fail_channel;
    }
    ret = rt_device_open(timer, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK)
    {
        goto fail_bound;
    }
    ret = rt_device_control(timer, HWTIMER_CTRL_FREQ_SET, &frequency);
    if (ret != RT_EOK)
    {
        goto fail_timer;
    }
    ret = rt_device_control(timer, HWTIMER_CTRL_MODE_SET, &mode);
    if (ret != RT_EOK)
    {
        goto fail_timer;
    }
    if (!infrared_device_tx_indicate(ir))
    {
        ret = -RT_EINVAL;
        goto fail_timer;
    }
    ret = rt_device_set_rx_indicate(timer, infrared_device_tx_indicate(ir));
    if (ret != RT_EOK)
    {
        goto fail_callback;
    }
    LOG_I("%s: PWM output opened", ir->config.name);
    return RT_EOK;

fail_callback:
    detach_ret = rt_device_set_rx_indicate(timer, RT_NULL);
    if (detach_ret != RT_EOK)
    {
        LOG_E("%s: PWM callback rollback failed (%d)", ir->config.name, detach_ret);
    }
    /* Try physical close once even if callback detachment failed, but do not
     * release the ISR pool unless both teardown operations succeeded. */
    close_ret = rt_device_close(timer);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: PWM timer rollback failed (%d)", ir->config.name, close_ret);
    }
    if (detach_ret != RT_EOK || close_ret != RT_EOK)
    {
        goto fail_retained;
    }
    goto fail_bound;
fail_timer:
    close_ret = rt_device_close(timer);
    if (close_ret != RT_EOK)
    {
        LOG_E("%s: PWM timer rollback failed (%d)", ir->config.name, close_ret);
        goto fail_retained;
    }
fail_bound:
    (void)infrared_device_bind_physical(ir, RT_NULL, IR_RESOURCE_OUTPUT);
fail_channel:
    (void)infrared_device_bind_channel(ir, RT_NULL, 0, 0U);
fail_pool:
    infrared_pwm_release(ir);
    goto fail_retained;
fail_set:
    close_ret = rt_pwm_disable(pwm, cfg->pwm_channel);
    if (close_ret == RT_EOK)
    {
        goto fail_channel;
    }
    LOG_E("%s: PWM disable rollback failed (%d)", ir->config.name, close_ret);
fail_retained:
    LOG_E("%s: PWM open failed (%d)", ir->config.name, ret);
    return ret;
}

/**
 * @brief Begin one envelope transfer with the original 500-us initial gap.
 * @param ir Transmitter whose backend-owned pulse buffer has been filled.
 * @return RT_EOK if the first timer interval has been armed.
 */
static rt_err_t infrared_pwm_start(struct infrared_device *ir)
{
    const struct infrared_pwm_config *cfg =
        (const struct infrared_pwm_config *)ir->config.output.settings;
    rt_err_t ret;
    struct infrared_pwm_state *state = (struct infrared_pwm_state *)ir->tx.user_data; /* Active transfer. */
    state->index = 0;
    ret = rt_pwm_disable(infrared_pwm_carrier(ir), cfg->pwm_channel);
    if (ret == RT_EOK)
    {
        ret = infrared_pwm_arm(ir, INFRARED_TX_START_DELAY_US);
    }
    if (ret != RT_EOK)
    {
        state->transfer.count = 0;
    }
    return ret;
}

/**
 * @brief Stop timer and carrier before the sender may release its pulse buffer.
 * @param ir Transmitter whose active hardware must be quiescent.
 * @return RT_EOK or the first physical-device stop error.
 */
static rt_err_t infrared_pwm_stop(struct infrared_device *ir)
{
    const struct infrared_pwm_config *cfg =
        (const struct infrared_pwm_config *)ir->config.output.settings;
    rt_err_t ret = RT_EOK;
    rt_err_t pwm_ret;
    struct infrared_pwm_state *state = (struct infrared_pwm_state *)ir->tx.user_data; /* Active transfer. */
    if (ir->tx.claimed_output)
    {
        ret = rt_device_control(ir->tx.claimed_output, HWTIMER_CTRL_STOP, RT_NULL);
    }
    if (state)
    {
        state->transfer.count = 0;
    }
    if (infrared_pwm_carrier(ir))
    {
        pwm_ret = rt_pwm_disable(infrared_pwm_carrier(ir), cfg->pwm_channel);
        if (ret == RT_EOK)
        {
            ret = pwm_ret;
        }
    }
    return ret;
}

/**
 * @brief Attempt one PWM stop, callback detach, and timer close before releasing claims.
 * @param ir Transmitter with an already completed or aborted transfer.
 * @return RT_EOK or the first stop, callback detach or timer close error.
 * @note Failure is reported to the caller; no automatic teardown retry is performed.
 */
static rt_err_t infrared_pwm_close(struct infrared_device *ir)
{
    rt_err_t ret;
    rt_err_t detach_ret = RT_EOK; /* Timer callback removal outcome. */
    rt_err_t timer_ret = RT_EOK; /* Physical timer close outcome. */
    if (!ir->tx.user_data && !ir->tx.claimed_output && !ir->tx.claimed_channel)
    {
        return RT_EOK;
    }
    /* Stop the carrier, detach its ISR and close the timer once. On any failed
     * teardown, retain the static ISR buffer and claims; the caller/BSP owns recovery. */
    ret = infrared_pwm_stop(ir);
    if (ret != RT_EOK)
    {
        LOG_E("%s: PWM stop failed (%d)", ir->config.name, ret);
    }
    if (ir->tx.claimed_output)
    {
        detach_ret = rt_device_set_rx_indicate(ir->tx.claimed_output, RT_NULL);
        if (detach_ret != RT_EOK)
        {
            LOG_E("%s: PWM timer callback detach failed (%d)", ir->config.name, detach_ret);
        }
        timer_ret = rt_device_close(ir->tx.claimed_output);
        if (timer_ret != RT_EOK)
        {
            LOG_E("%s: PWM timer close failed (%d)", ir->config.name, timer_ret);
        }
    }
    if (ret == RT_EOK && detach_ret == RT_EOK && timer_ret == RT_EOK)
    {
        if (ir->tx.claimed_output)
        {
            (void)infrared_device_bind_physical(ir, RT_NULL, IR_RESOURCE_OUTPUT);
        }
        (void)infrared_device_bind_channel(ir, RT_NULL, 0, 0U);
        infrared_pwm_release(ir);
        return RT_EOK;
    }
    if (ret == RT_EOK)
    {
        ret = detach_ret == RT_EOK ? timer_ret : detach_ret;
    }
    return ret;
}

/**
 * @brief Immutable PWM/HWTIMER adapter selected by infrared_device_register.
 */
const struct infrared_backend_ops infrared_pwm_ops = {
    .name = "pwm",
    .direction = INFRARED_BACKEND_OUTPUT,
    .open = infrared_pwm_open,
    .start = infrared_pwm_start,
    .stop = infrared_pwm_stop,
    .close = infrared_pwm_close,
    .io.tx = { infrared_pwm_timeout }
};
