/**
 * @file infrared_framework_config.h
 * @brief Build-time capacity defaults and feature derivation for the new framework.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_FRAMEWORK_CONFIG_H
#define INFRARED_FRAMEWORK_CONFIG_H

/** @brief Permanent registry capacity; unused indices consume no live hardware resources. */
#ifndef INFRARED_MAX_DEVICES
#define INFRARED_MAX_DEVICES 3
#endif /* !defined(INFRARED_MAX_DEVICES) */

/** @brief Maximum logical indices addressable by the example event bitset. */
#define INFRARED_CALLBACK_CAPACITY 16U
#if INFRARED_MAX_DEVICES < 1 || INFRARED_MAX_DEVICES > INFRARED_CALLBACK_CAPACITY
#error "INFRARED_MAX_DEVICES must be in range 1..16"
#endif /* INFRARED_MAX_DEVICES < 1 || INFRARED_MAX_DEVICES > INFRARED_CALLBACK_CAPACITY */

/** @brief Static complete-frame records reserved for each registered device. */
#ifndef INFRARED_FRAME_CAPACITY
#define INFRARED_FRAME_CAPACITY 4
#endif /* !defined(INFRARED_FRAME_CAPACITY) */

/** @brief Static packed-pulse entries per transmitter; NEC needs 68 for a full frame. */
#ifndef INFRARED_TX_PULSE_CAPACITY
#define INFRARED_TX_PULSE_CAPACITY 128
#endif /* !defined(INFRARED_TX_PULSE_CAPACITY) */

/** @brief Envelope completion timeout in RT-Thread ticks; 0 retains indefinite-wait behavior. */
#ifndef INFRARED_TX_TIMEOUT_TICKS
#define INFRARED_TX_TIMEOUT_TICKS 0
#endif /* !defined(INFRARED_TX_TIMEOUT_TICKS) */

/** @brief PIN ISR FIFO capacity measured in packed 32-bit pulse entries. */
#ifndef INFRARED_PIN_PULSE_CAPACITY
#define INFRARED_PIN_PULSE_CAPACITY 200
#endif /* !defined(INFRARED_PIN_PULSE_CAPACITY) */

/** @brief Compact NEC parser instance capacity when the codec is enabled. */
#ifndef INFRARED_NEC_CONTEXT_COUNT
#define INFRARED_NEC_CONTEXT_COUNT INFRARED_MAX_DEVICES
#endif /* !defined(INFRARED_NEC_CONTEXT_COUNT) */

/** @brief Maximum protocol frame payload length in bytes. */
#ifndef INFRARED_FRAME_MAX_BYTES
#define INFRARED_FRAME_MAX_BYTES 16
#endif /* !defined(INFRARED_FRAME_MAX_BYTES) */

/** @brief NEC pulse duration deviation tolerance, in microseconds. */
#ifndef INFRARED_NEC_TOLERANCE_US
#define INFRARED_NEC_TOLERANCE_US 210
#endif /* !defined(INFRARED_NEC_TOLERANCE_US) */

/** @brief Master runtime-validation gate; Kconfig disabled symbols are absent. */
#if defined(INFRARED_CHECK_ALL)
#define IR_CFG_CHECK_ALL 1
#else
#define IR_CFG_CHECK_ALL 0
#endif /* defined(INFRARED_CHECK_ALL) */

/** @brief Optional caller argument validation. */
#if defined(INFRARED_CHECK_PARAMETERS)
#define IR_CFG_CHECK_PARAMETERS IR_CFG_CHECK_ALL
#else
#define IR_CFG_CHECK_PARAMETERS 0
#endif /* defined(INFRARED_CHECK_PARAMETERS) */

/** @brief Optional backend and codec contract validation. */
#if defined(INFRARED_CHECK_CONFIG)
#define IR_CFG_CHECK_CONFIG IR_CFG_CHECK_ALL
#else
#define IR_CFG_CHECK_CONFIG 0
#endif /* defined(INFRARED_CHECK_CONFIG) */

/** @brief Optional registration/resource preflight checks. */
#if defined(INFRARED_CHECK_RESOURCE)
#define IR_CFG_CHECK_RESOURCE IR_CFG_CHECK_ALL
#else
#define IR_CFG_CHECK_RESOURCE 0
#endif /* defined(INFRARED_CHECK_RESOURCE) */

/** @brief Optional hardware diagnostics (not mandatory cleanup). */
#if defined(INFRARED_CHECK_DEVICE)
#define IR_CFG_CHECK_DEVICE IR_CFG_CHECK_ALL
#else
#define IR_CFG_CHECK_DEVICE 0
#endif /* defined(INFRARED_CHECK_DEVICE) */

/** @brief Manual backend capacity defaults to zero when disabled in Kconfig. */
#ifndef INFRARED_MANUAL_PIN_CAPACITY
#define INFRARED_MANUAL_PIN_CAPACITY 0
#endif /* !defined(INFRARED_MANUAL_PIN_CAPACITY) */
#ifndef INFRARED_MANUAL_CAPTURE_CAPACITY
#define INFRARED_MANUAL_CAPTURE_CAPACITY 0
#endif /* !defined(INFRARED_MANUAL_CAPTURE_CAPACITY) */
#ifndef INFRARED_MANUAL_PWM_CAPACITY
#define INFRARED_MANUAL_PWM_CAPACITY 0
#endif /* !defined(INFRARED_MANUAL_PWM_CAPACITY) */

/** @brief Number of compile-time enabled backends and the explicitly reserved manual instances. */
enum infrared_configured_capacity
{
    /** Configured GPIO PIN+HWTIMER receiver pool size. */
    IR_CONFIGURED_PIN_COUNT = INFRARED_MANUAL_PIN_CAPACITY
#ifdef INFRARED_DEV0_INPUT_PIN
        + 1
#endif /* INFRARED_DEV0_INPUT_PIN */
#ifdef INFRARED_DEV1_INPUT_PIN
        + 1
#endif /* INFRARED_DEV1_INPUT_PIN */
#ifdef INFRARED_DEV2_INPUT_PIN
        + 1
#endif /* INFRARED_DEV2_INPUT_PIN */
        ,
    /** Configured Input Capture receiver pool size. */
    IR_CONFIGURED_CAPTURE_COUNT = INFRARED_MANUAL_CAPTURE_CAPACITY
#ifdef INFRARED_DEV0_INPUT_CAPTURE
        + 1
#endif /* INFRARED_DEV0_INPUT_CAPTURE */
#ifdef INFRARED_DEV1_INPUT_CAPTURE
        + 1
#endif /* INFRARED_DEV1_INPUT_CAPTURE */
#ifdef INFRARED_DEV2_INPUT_CAPTURE
        + 1
#endif /* INFRARED_DEV2_INPUT_CAPTURE */
        ,
    /** Configured PWM+HWTIMER transmitter pool size. */
    IR_CONFIGURED_TX_COUNT = INFRARED_MANUAL_PWM_CAPACITY
#ifdef INFRARED_DEV0_OUTPUT
        + 1
#endif /* INFRARED_DEV0_OUTPUT */
#ifdef INFRARED_DEV1_OUTPUT
        + 1
#endif /* INFRARED_DEV1_OUTPUT */
#ifdef INFRARED_DEV2_OUTPUT
        + 1
#endif /* INFRARED_DEV2_OUTPUT */
        ,
    /** Total completed-frame FIFO pool size across both input backends. */
    IR_CONFIGURED_RX_COUNT = IR_CONFIGURED_PIN_COUNT + IR_CONFIGURED_CAPTURE_COUNT
};

/** @brief Whether the configured RX pool contains at least one receiving instance. */
#if INFRARED_MANUAL_PIN_CAPACITY > 0 || INFRARED_MANUAL_CAPTURE_CAPACITY > 0 || \
    defined(INFRARED_DEV0_INPUT_PIN) || defined(INFRARED_DEV1_INPUT_PIN) ||     \
    defined(INFRARED_DEV2_INPUT_PIN) || defined(INFRARED_DEV0_INPUT_CAPTURE) || \
    defined(INFRARED_DEV1_INPUT_CAPTURE) || defined(INFRARED_DEV2_INPUT_CAPTURE)
#define IR_CFG_RX_POOL_ENABLED 1
#else
#define IR_CFG_RX_POOL_ENABLED 0
#endif /* configured RX pool */

/** @brief Whether the configured PIN pool contains at least one receiver. */
#if INFRARED_MANUAL_PIN_CAPACITY > 0 || defined(INFRARED_DEV0_INPUT_PIN) || \
    defined(INFRARED_DEV1_INPUT_PIN) || defined(INFRARED_DEV2_INPUT_PIN)
#define IR_CFG_PIN_POOL_ENABLED 1
#else
#define IR_CFG_PIN_POOL_ENABLED 0
#endif /* configured PIN pool */

/** @brief Whether the configured PWM pool contains at least one transmitter. */
#if INFRARED_MANUAL_PWM_CAPACITY > 0 || defined(INFRARED_DEV0_OUTPUT) || \
    defined(INFRARED_DEV1_OUTPUT) || defined(INFRARED_DEV2_OUTPUT)
#define IR_CFG_PWM_POOL_ENABLED 1
#else
#define IR_CFG_PWM_POOL_ENABLED 0
#endif /* configured PWM pool */

/** @brief PIN adapter availability requires the corresponding RT-Thread drivers. */
#if defined(INFRARED_DEVICE_PIN) && defined(RT_USING_PIN) && defined(RT_USING_HWTIMER)
#define IR_CFG_PIN_INPUT 1
#else
#define IR_CFG_PIN_INPUT 0
#endif /* defined(INFRARED_DEVICE_PIN) && defined(RT_USING_PIN) && defined(RT_USING_HWTIMER) */

/** @brief Input Capture adapter availability requires RT-Thread Input Capture. */
#if defined(INFRARED_USING_INPUT_CAPTURE) && defined(RT_USING_INPUT_CAPTURE)
#define IR_CFG_CAPTURE_INPUT 1
#else
#define IR_CFG_CAPTURE_INPUT 0
#endif /* defined(INFRARED_USING_INPUT_CAPTURE) && defined(RT_USING_INPUT_CAPTURE) */

/** @brief Output availability requires both PWM and HWTIMER. */
#if defined(INFRARED_DEVICE_PWM) && defined(RT_USING_PWM) && defined(RT_USING_HWTIMER)
#define IR_CFG_PWM_OUTPUT 1
#else
#define IR_CFG_PWM_OUTPUT 0
#endif /* defined(INFRARED_DEVICE_PWM) && defined(RT_USING_PWM) && defined(RT_USING_HWTIMER) */

/** @brief True if either independent input adapter is compiled. */
#if IR_CFG_PIN_INPUT || IR_CFG_CAPTURE_INPUT
#define IR_CFG_INPUT 1
#else
#define IR_CFG_INPUT 0
#endif /* IR_CFG_PIN_INPUT || IR_CFG_CAPTURE_INPUT */

#if INFRARED_NEC_CONTEXT_COUNT < 1 || INFRARED_NEC_CONTEXT_COUNT > INFRARED_MAX_DEVICES
#error "NEC parser context count must be 1..INFRARED_MAX_DEVICES"
#endif /* NEC context count */

/** @brief Enforce per-direction pool capacities against the logical registry size. */
typedef char infrared_configured_pool_capacity_check[
    IR_CONFIGURED_RX_COUNT <= INFRARED_MAX_DEVICES &&
    IR_CONFIGURED_TX_COUNT <= INFRARED_MAX_DEVICES ? 1 : -1];

#if INFRARED_FRAME_CAPACITY < 1
#error "Infrared frame capacity must be positive"
#endif /* INFRARED_FRAME_CAPACITY < 1 */

#endif /* INFRARED_FRAMEWORK_CONFIG_H */
