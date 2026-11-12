/**
 * @file pulse_distance.c
 * @brief Shared pulse-distance envelope handling for standard and custom IR codecs.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 *
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#include "pulse_distance.h"

/**
 * @brief Compare level and duration according to a protocol's timing policy.
 * @param pulse Normalized envelope segment.
 * @param level Required MARK/SPACE tag.
 * @param expected_us Nominal duration.
 * @param tolerance_us Allowed absolute duration error.
 * @param strict Require strict rather than inclusive bounds.
 * @return RT_TRUE when both level and duration match.
 */
static rt_bool_t infrared_pd_match(infrared_pulse_t pulse, rt_uint32_t level,
                                   rt_uint32_t expected_us, rt_uint32_t tolerance_us,
                                   rt_bool_t strict)
{
    rt_uint32_t actual = INFRARED_PULSE_DURATION(pulse); /* Measured interval in microseconds. */
    if (INFRARED_PULSE_LEVEL(pulse) != level || expected_us <= tolerance_us)
    {
        return RT_FALSE;
    }
    if (strict)
    {
        return actual > expected_us - tolerance_us && actual < expected_us + tolerance_us;
    }
    return actual >= expected_us - tolerance_us && actual <= expected_us + tolerance_us;
}

/**
 * @brief Reset an incomplete envelope to its initial GUIDE_MARK state.
 * @param decoder Protocol-owned parser context; protocol history is unchanged.
 */
void infrared_pd_reset(struct infrared_pd_decoder *decoder)
{
    decoder->stage = INFRARED_PD_GUIDE_MARK;
    decoder->word = 0;
    decoder->bits = 0;
}

/**
 * @brief Parse one physical interval with the protocol-selected pulse timings.
 * @param decoder Persistent receiver state.
 * @param timing Pulse duration and boundary matching policy.
 * @param pulse Normalized MARK or SPACE interval in microseconds.
 * @param allow_repeat Permit the optional short repeat path after a prior valid frame.
 * @param word Output raw MSB-first word on INFRARED_PD_FRAME only.
 * @return NONE while incomplete, FRAME for a whole data word, or REPEAT.
 * @note Invalid pulses reset the state; optional leader re-sync inspects the
 *       same pulse once more without consuming a second input sample.
 */
enum infrared_pd_event infrared_pd_step(struct infrared_pd_decoder *decoder,
                                        const struct infrared_pd_timing *timing,
                                        infrared_pulse_t pulse, rt_bool_t allow_repeat,
                                        rt_uint32_t *word)
{
    rt_bool_t invalid = RT_FALSE;                     /* Whether this interval invalidates the partial envelope. */
    enum infrared_pd_event result = INFRARED_PD_NONE; /* Completed frame or repeat indication. */
    switch (decoder->stage)
    {
    case INFRARED_PD_GUIDE_MARK:
        if (infrared_pd_match(pulse, INFRARED_MARK, timing->guide_mark_us,
                              timing->guide_mark_tol_us, timing->strict_bounds))
        {
            decoder->stage = INFRARED_PD_GUIDE_SPACE;
        }
        break;
    case INFRARED_PD_GUIDE_SPACE:
        if (infrared_pd_match(pulse, INFRARED_SPACE, timing->guide_space_us,
                              timing->guide_space_tol_us, timing->strict_bounds))
        {
            decoder->word = 0;
            decoder->bits = 0;
            decoder->stage = INFRARED_PD_DATA_MARK;
        }
        else if (allow_repeat && timing->repeat_space_us &&
                 infrared_pd_match(pulse, INFRARED_SPACE, timing->repeat_space_us,
                                   timing->repeat_space_tol_us, timing->strict_bounds))
        {
            decoder->stage = INFRARED_PD_REPEAT_MARK;
        }
        else
        {
            invalid = RT_TRUE;
        }
        break;
    case INFRARED_PD_DATA_MARK:
        if (infrared_pd_match(pulse, INFRARED_MARK, timing->data_mark_us,
                              timing->data_mark_tol_us, timing->strict_bounds))
        {
            decoder->stage = INFRARED_PD_DATA_SPACE;
        }
        else
        {
            invalid = RT_TRUE;
        }
        break;
    case INFRARED_PD_DATA_SPACE:
        if (infrared_pd_match(pulse, INFRARED_SPACE, timing->one_space_us,
                              timing->data_space_tol_us, timing->strict_bounds))
        {
            decoder->word = (decoder->word << 1) | 1U;
        }
        else if (infrared_pd_match(pulse, INFRARED_SPACE, timing->zero_space_us,
                                   timing->data_space_tol_us, timing->strict_bounds))
        {
            decoder->word <<= 1;
        }
        else
        {
            invalid = RT_TRUE;
            break;
        }
        decoder->bits++;
        decoder->stage = decoder->bits == timing->data_bits ? INFRARED_PD_TRAILER_MARK : INFRARED_PD_DATA_MARK;
        break;
    case INFRARED_PD_TRAILER_MARK:
        if ((!timing->check_trailer_timing && INFRARED_PULSE_LEVEL(pulse) == INFRARED_MARK) ||
            (timing->check_trailer_timing &&
             infrared_pd_match(pulse, INFRARED_MARK, timing->trailer_mark_us,
                               timing->trailer_mark_tol_us, timing->strict_bounds)))
        {
            *word = decoder->word;
            result = INFRARED_PD_FRAME;
            infrared_pd_reset(decoder);
        }
        else
        {
            invalid = RT_TRUE;
        }
        break;
    case INFRARED_PD_REPEAT_MARK:
        if (infrared_pd_match(pulse, INFRARED_MARK, timing->data_mark_us,
                              timing->data_mark_tol_us, timing->strict_bounds))
        {
            result = INFRARED_PD_REPEAT;
            infrared_pd_reset(decoder);
        }
        else
        {
            invalid = RT_TRUE;
        }
        break;
    default:
        invalid = RT_TRUE;
        break;
    }
    if (invalid)
    {
        infrared_pd_reset(decoder);
        if (timing->resync_on_invalid &&
            infrared_pd_match(pulse, INFRARED_MARK, timing->guide_mark_us,
                              timing->guide_mark_tol_us, timing->strict_bounds))
        {
            decoder->stage = INFRARED_PD_GUIDE_SPACE;
        }
    }
    return result;
}

/**
 * @brief Expand one packed word into leader, data MARK/SPACE pairs and trailer.
 * @param timing Bit count and physical durations supplied by the protocol codec.
 * @param word Packed data, transmitted MSB first without byte reversal.
 * @param pulses Caller-owned pulse record array.
 * @param capacity Total number of writable pulse records.
 * @return Exactly 4 + 2 * data_bits records on success, otherwise zero.
 * @note Does not add NEC inverse bytes or compute MR checksum; both are
 *       protocol-layer responsibilities before calling this helper.
 */
rt_size_t infrared_pd_encode(const struct infrared_pd_timing *timing, rt_uint32_t word,
                             infrared_pulse_t *pulses, rt_size_t capacity)
{
    rt_size_t index;                                             /* Current frame bit number and corresponding pair position. */
    rt_size_t required = 4U + (rt_size_t)timing->data_bits * 2U; /* Two leader, bit pairs, two trailer records. */
    if (!timing->data_bits || timing->data_bits > 32U || capacity < required)
    {
        return 0;
    }
    pulses[0] = INFRARED_PULSE_PACK(INFRARED_MARK, timing->guide_mark_us);
    pulses[1] = INFRARED_PULSE_PACK(INFRARED_SPACE, timing->guide_space_us);
    for (index = 0; index < timing->data_bits; ++index)
    {
        rt_uint32_t bit = (word >> (timing->data_bits - index - 1U)) & 1U; /* MSB-first protocol bit. */
        pulses[2U + index * 2U] = INFRARED_PULSE_PACK(INFRARED_MARK, timing->data_mark_us);
        pulses[3U + index * 2U] = INFRARED_PULSE_PACK(INFRARED_SPACE,
                                                      bit ? timing->one_space_us : timing->zero_space_us);
    }
    pulses[required - 2U] = INFRARED_PULSE_PACK(INFRARED_MARK,
                                                timing->trailer_mark_us);
    pulses[required - 1U] = INFRARED_PULSE_PACK(INFRARED_SPACE, timing->frame_gap_us);
    return required;
}
