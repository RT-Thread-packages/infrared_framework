/**
 * @file infrared_pwm.h
 * @brief PWM carrier plus HWTIMER envelope output configuration and private state.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_PWM_H
#define INFRARED_PWM_H

#include <rtdevice.h>
#include "infrared_backend.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/** @brief Immutable PWM carrier and envelope timer settings. */
struct infrared_pwm_config
{
    const char *pwm_name;   /**< BSP PWM device name used as a carrier. */
    rt_int32_t pwm_channel; /**< Exclusive output channel on the PWM device. */
    const char *timer_name; /**< HWTIMER producing variable-duration envelope segments. */
    rt_uint32_t period_ns;  /**< Carrier period in nanoseconds; zero chooses legacy 38 kHz. */
    rt_uint32_t pulse_ns;   /**< Carrier active time in nanoseconds; zero chooses legacy duty. */
};

/** @brief Selectable PWM/HWTIMER transmit implementation. */
extern const struct infrared_backend_ops infrared_pwm_ops;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INFRARED_PWM_H */
