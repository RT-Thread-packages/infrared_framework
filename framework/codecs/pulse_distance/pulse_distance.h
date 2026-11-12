/**
 * @file pulse_distance.h
 * @brief Reusable MSB-first pulse-distance envelope encoder and streaming decoder.
 * @details Protocol validation and frame layouts remain in the owning codec.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 *
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef INFRARED_PULSE_DISTANCE_H
#define INFRARED_PULSE_DISTANCE_H

#include "infrared_codec.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * @brief Shared pulse-distance receive parser states for NEC and MR.
 * @verbatim
 * [GUIDE_MARK] -- valid leader MARK --> [GUIDE_SPACE]
 *                                      |              |
 *                              normal SPACE    optional repeat SPACE
 *                                      |              |
 *                                      v              v
 *                                [DATA_MARK]     [REPEAT_MARK]
 *                                      |              | final MARK
 *                                  bit MARK           v
 *                                      v         emit repeat/reset
 *                                [DATA_SPACE]
 *                                      | bit SPACE: shift MSB-first
 *                          bits < N: --+--> [DATA_MARK]
 *                          bits == N: v
 *                                [TRAILER_MARK]
 *                                      | final MARK
 *                                      v
 *                               emit word / reset
 *
 * Invalid pulse --> reset to GUIDE_MARK; resync_on_invalid may treat
 * that same pulse as a new leader. Per-protocol checksum is not shared.
 * @endverbatim
 */
enum infrared_pd_stage
{
    INFRARED_PD_GUIDE_MARK,   /**< Waiting for the leader's carrier interval. */
    INFRARED_PD_GUIDE_SPACE,  /**< Waiting for the leader's idle interval. */
    INFRARED_PD_DATA_MARK,    /**< Waiting for a carrier before a data bit. */
    INFRARED_PD_DATA_SPACE,   /**< Waiting for the data bit's idle interval. */
    INFRARED_PD_TRAILER_MARK, /**< Waiting for the carrier following the last data bit. */
    INFRARED_PD_REPEAT_MARK   /**< Waiting for an optional repeat frame's final carrier. */
};

/** @brief Result of one envelope sample; no protocol checksum is performed here. */
enum infrared_pd_event
{
    INFRARED_PD_NONE,  /**< No complete frame yet. */
    INFRARED_PD_FRAME, /**< One full raw data word was received. */
    INFRARED_PD_REPEAT /**< A short repeat header was received. */
};

/** @brief Persistent data-bit state; allocated and owned by the protocol codec. */
struct infrared_pd_decoder
{
    enum infrared_pd_stage stage; /**< Phase of the current pulse-distance envelope. */
    rt_uint32_t word;             /**< Raw data bits accumulated in transmitted order. */
    rt_uint8_t bits;              /**< Number of decoded bits in the current word. */
};

/**
 * @brief Protocol-selected pulse timings and receive acceptance policies, in microseconds.
 * @note A zero repeat gap disables short repeat parsing. All nonzero
 *       receive intervals must exceed their tolerance.
 */
struct infrared_pd_timing
{
    rt_uint32_t guide_mark_us;       /**< Leader carrier duration. */
    rt_uint32_t guide_mark_tol_us;   /**< Leader carrier tolerance. */
    rt_uint32_t guide_space_us;      /**< Leader gap duration. */
    rt_uint32_t guide_space_tol_us;  /**< Leader gap tolerance. */
    rt_uint32_t data_mark_us;        /**< Every data bit's carrier duration. */
    rt_uint32_t data_mark_tol_us;    /**< Data carrier tolerance. */
    rt_uint32_t zero_space_us;       /**< Idle duration representing zero. */
    rt_uint32_t one_space_us;        /**< Idle duration representing one. */
    rt_uint32_t data_space_tol_us;   /**< Tolerance for zero/one spaces. */
    rt_uint32_t trailer_mark_us;     /**< Trailing carrier duration, used when encoding. */
    rt_uint32_t trailer_mark_tol_us; /**< Trailing carrier tolerance. */
    rt_uint32_t repeat_space_us;     /**< Optional short-repeat idle duration, or zero to disable. */
    rt_uint32_t repeat_space_tol_us; /**< Short-repeat gap tolerance. */
    rt_uint32_t frame_gap_us;        /**< Trailing idle emitted after a complete frame. */
    rt_uint8_t data_bits;            /**< Number of data bits transmitted MSB-first (1..32). */
    rt_bool_t check_trailer_timing;  /**< Validate received trailer duration, not only MARK level. */
    rt_bool_t strict_bounds;         /**< Require open duration bounds instead of inclusive ones. */
    rt_bool_t resync_on_invalid;     /**< Allow a bad segment to become the next leader MARK. */
};

/**
 * @brief Reset the raw envelope parser, without changing protocol-owned history.
 * @param decoder Protocol-owned persistent envelope context.
 */
void infrared_pd_reset(struct infrared_pd_decoder *decoder);

/**
 * @brief Decode exactly one normalized envelope interval.
 * @param decoder Persistent envelope state.
 * @param timing Immutable pulse timing and matching policy.
 * @param pulse Packed MARK or SPACE with duration in microseconds.
 * @param allow_repeat Whether the protocol has a validated frame to repeat.
 * @param word Raw MSB-first word populated only on INFRARED_PD_FRAME.
 * @return INFRARED_PD_NONE, INFRARED_PD_FRAME or INFRARED_PD_REPEAT.
 */
enum infrared_pd_event infrared_pd_step(struct infrared_pd_decoder *decoder,
                                        const struct infrared_pd_timing *timing,
                                        infrared_pulse_t pulse, rt_bool_t allow_repeat,
                                        rt_uint32_t *word);

/**
 * @brief Generate one leader/data/trailer envelope from an already packed word.
 * @param timing Immutable timing parameters describing the word format.
 * @param word Data bits in the low data_bits positions, high bit first.
 * @param pulses Caller-owned pulse array.
 * @param capacity Number of available pulse records.
 * @return Number of pulses emitted (4 + 2 * data_bits), or zero if insufficient.
 */
rt_size_t infrared_pd_encode(const struct infrared_pd_timing *timing, rt_uint32_t word,
                             infrared_pulse_t *pulses, rt_size_t capacity);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* INFRARED_PULSE_DISTANCE_H */
