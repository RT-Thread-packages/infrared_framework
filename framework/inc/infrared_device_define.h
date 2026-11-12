/**
 * @file infrared_device_define.h
 * @brief Reusable compile-time declarations for configured infrared instances.
 * @details Each configured slot provides INFRARED_DEVn_NAME, RX_BACKEND,
 *          TX_BACKEND and the selected backend settings. Call IR_DEFINE_DEVICE
 *          once per enabled slot with the chosen codec operations pointer.
 *          This header generates ordinary C callbacks; no runtime ISR lookup
 *          or external source generator is required.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef INFRARED_DEVICE_DEFINE_H
#define INFRARED_DEVICE_DEFINE_H

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

/** @brief Declare the static PIN/HWTIMER configuration for one enabled slot. */
#define IR_DECLARE_PIN(n) \
    static const struct infrared_pin_config ir_cfg_pin_##n = { INFRARED_DEV##n##_RX_TIMER, INFRARED_DEV##n##_PIN, INFRARED_DEV##n##_ACTIVE_LEVEL, 0U };

/**
 * @brief Declare the immutable Capture settings and fixed-index ISR callback for slot n.
 * @param n Constant configured index; the generated callback accepts physical and size,
 *          and forwards both to infrared_device_dispatch_rx(), returning its status.
 */
#define IR_DECLARE_CAPTURE(n)                                                                                                                \
    static const struct infrared_capture_config ir_cfg_capture_##n = { INFRARED_DEV##n##_CAPTURE_NAME, INFRARED_DEV##n##_ACTIVE_LEVEL, 1U }; \
    static rt_err_t ir_cfg_capture_callback_##n(rt_device_t physical, rt_size_t size)                                                        \
    {                                                                                                                                        \
        return infrared_device_dispatch_rx(n, physical, size);                                                                               \
    }

/**
 * @brief Declare immutable PWM settings and a fixed-index envelope callback for slot n.
 * @param n Constant configured index; the generated callback accepts physical and size,
 *          and forwards both to infrared_device_dispatch_tx(), returning its status.
 */
#define IR_DECLARE_PWM(n)                                                                                                                                    \
    static const struct infrared_pwm_config ir_cfg_pwm_##n = { INFRARED_DEV##n##_TX_PWM, INFRARED_DEV##n##_TX_CHANNEL, INFRARED_DEV##n##_TX_TIMER, 0U, 0U }; \
    static rt_err_t ir_cfg_pwm_callback_##n(rt_device_t physical, rt_size_t size)                                                                            \
    {                                                                                                                                                        \
        return infrared_device_dispatch_tx(n, physical, size);                                                                                               \
    }

/** @brief Reuse backend initializers, rather than repeating function and settings names. */
#define IR_RX_PIN(n)     { &infrared_pin_ops, &ir_cfg_pin_##n, RT_NULL }
#define IR_RX_CAPTURE(n) { &infrared_capture_ops, &ir_cfg_capture_##n, ir_cfg_capture_callback_##n }
#define IR_RX_NONE       { RT_NULL, RT_NULL, RT_NULL }
#define IR_TX_PWM(n)     { &infrared_pwm_ops, &ir_cfg_pwm_##n, INFRARED_TX_TIMEOUT_TICKS, ir_cfg_pwm_callback_##n }
#define IR_TX_NONE       { RT_NULL, RT_NULL, 0, RT_NULL }

/** @brief Declare no RX hardware for backend 0 (none). */
#define IR_DECLARE_RX_0(n)
/** @brief Declare PIN settings for backend 1. */
#define IR_DECLARE_RX_1(n) IR_DECLARE_PIN(n)
/** @brief Declare Capture settings and callback for backend 2. */
#define IR_DECLARE_RX_2(n) IR_DECLARE_CAPTURE(n)
/** @brief Initialize a disabled RX backend. */
#define IR_RX_INIT_0(n) IR_RX_NONE
/** @brief Initialize a PIN receiver backend. */
#define IR_RX_INIT_1(n) IR_RX_PIN(n)
/** @brief Initialize a Capture receiver backend. */
#define IR_RX_INIT_2(n) IR_RX_CAPTURE(n)

/** @brief Declare no TX hardware for backend 0 (none). */
#define IR_DECLARE_TX_0(n)
/** @brief Declare PWM settings and callback for backend 1. */
#define IR_DECLARE_TX_1(n) IR_DECLARE_PWM(n)
/** @brief Initialize a disabled TX backend. */
#define IR_TX_INIT_0(n) IR_TX_NONE
/** @brief Initialize a PWM transmitter backend. */
#define IR_TX_INIT_1(n) IR_TX_PWM(n)

/** @brief Look up Kconfig's numeric input backend for numbered slot n. */
#define IR_SLOT_RX_BACKEND(n) INFRARED_DEV##n##_RX_BACKEND
/** @brief Look up Kconfig's numeric output backend for numbered slot n. */
#define IR_SLOT_TX_BACKEND(n) INFRARED_DEV##n##_TX_BACKEND

/** @brief Expand the selected backend into its state, callback, and device config. */
#define IR_DEFINE_DEVICE_BACKENDS(n, rx, tx, codec_ops)                                        \
    IR_DECLARE_RX_##rx(n)                                                                      \
        IR_DECLARE_TX_##tx(n) static const struct infrared_device_config ir_cfg_device_##n = { \
            .index = n,                                                                        \
            .name = INFRARED_DEV##n##_NAME,                                                    \
            .input = IR_RX_INIT_##rx(n),                                                       \
            .output = IR_TX_INIT_##tx(n),                                                      \
            .codec = { codec_ops, RT_NULL }                                                    \
        };

/** @brief Expand the selector values before token concatenation. */
#define IR_DEFINE_DEVICE_EXPAND(n, rx, tx, codec_ops) IR_DEFINE_DEVICE_BACKENDS(n, rx, tx, codec_ops)

/**
 * @brief Define one configured device and the fixed physical callbacks it requires.
 * @param n Logical index with INFRARED_DEVn_* build-time settings.
 * @param codec_ops Pointer to the registered codec operation table.
 */
#define IR_DEFINE_DEVICE(n, codec_ops) \
    IR_DEFINE_DEVICE_EXPAND(n, IR_SLOT_RX_BACKEND(n), IR_SLOT_TX_BACKEND(n), codec_ops)

#endif /* INFRARED_DEVICE_DEFINE_H */
