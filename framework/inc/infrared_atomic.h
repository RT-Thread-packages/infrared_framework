/**
 * @file infrared_atomic.h
 * @brief RT-Thread 3.x and 5.x atomic state compatibility.
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
#ifndef INFRARED_ATOMIC_H
#define INFRARED_ATOMIC_H

#include <rtthread.h>
#include <rthw.h>
#include "infrared_framework_config.h"

#if defined(RTTHREAD_VERSION) && (RTTHREAD_VERSION >= 50000)
#include <rtatomic.h>
/**
 * @brief Atomic integer used for per-device transmit admission.
 */
typedef rt_atomic_t infrared_atomic_t;

/**
 * @brief Read an atomic state value.
 * @param value Atomic state address.
 * @return State value.
 */
static inline rt_base_t infrared_atomic_load(volatile infrared_atomic_t *value)
{
    return (rt_base_t)rt_atomic_load(value);
}

/**
 * @brief Atomically replace an expected state value.
 * @param value Atomic state address.
 * @param expected Expected old value; replaced by actual value on failure.
 * @param desired New state value.
 * @return RT_TRUE when the exchange succeeded.
 */
static inline rt_bool_t infrared_atomic_compare_exchange(volatile infrared_atomic_t *value,
                                                         rt_base_t *expected, rt_base_t desired)
{
    rt_atomic_t old = (rt_atomic_t)*expected;
    rt_bool_t exchanged = rt_atomic_compare_exchange_strong(value, &old, (rt_atomic_t)desired);
    *expected = (rt_base_t)old;
    return exchanged;
}

/**
 * @brief Publish a new atomic state value.
 * @param value Atomic state address.
 * @param next New state value.
 */
static inline void infrared_atomic_store(volatile infrared_atomic_t *value, rt_base_t next)
{
    rt_atomic_store(value, (rt_atomic_t)next);
}
#else
/**
 * @brief RT-Thread pre-v5 atomic state emulated by short single-core IRQ critical sections.
 */
typedef rt_base_t infrared_atomic_t;

/**
 * @brief Read a state atomically on an RT-Thread pre-v5 single-core target.
 * @param value State address.
 * @return State value.
 */
static inline rt_base_t infrared_atomic_load(volatile infrared_atomic_t *value)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_base_t result = *value;
    rt_hw_interrupt_enable(level);
    return result;
}

/**
 * @brief Compare-and-swap a state using the target's interrupt critical section.
 * @param value State address.
 * @param expected Expected value; updated on failure.
 * @param desired Replacement value.
 * @return RT_TRUE only when the value matched and was replaced.
 */
static inline rt_bool_t infrared_atomic_compare_exchange(volatile infrared_atomic_t *value,
                                                         rt_base_t *expected, rt_base_t desired)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_bool_t success = (*value == *expected);
    if (success)
    {
        *value = desired;
    }
    else
    {
        *expected = *value;
    }
    rt_hw_interrupt_enable(level);
    return success;
}

/**
 * @brief Publish a state under a short IRQ critical section.
 * @param value State address.
 * @param next New state value.
 */
static inline void infrared_atomic_store(volatile infrared_atomic_t *value, rt_base_t next)
{
    rt_base_t level = rt_hw_interrupt_disable();
    *value = next;
    rt_hw_interrupt_enable(level);
}
#endif /* RTTHREAD_VERSION >= 50000 */

#endif /* INFRARED_ATOMIC_H */
