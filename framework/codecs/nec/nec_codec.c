/**
 * @file nec_codec.c
 * @brief Per-instance batch NEC pulse decoder and encoder for the infrared framework.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 *
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#include "nec_codec.h"
#include "pulse_distance.h"
#include "infrared_device.h"
#include "infrared_framework_config.h"

#define DBG_TAG "infrared.nec"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/** @brief One compact NEC slot owns its parser state and stable logical device index. */
struct infrared_nec_slot
{
    struct infrared_nec_context context; /**< Independent parser state for the owner. */
    rt_size_t owner;                     /**< Stable logical device index. */
    rt_bool_t used;                      /**< Whether this slot is reserved. */
};

/** @brief Compact NEC state pool; one entry per configured NEC receiver. */
static struct infrared_nec_slot infrared_nec_slots[INFRARED_NEC_CONTEXT_COUNT];

/**
 * @brief Resolve or reserve one NEC parser context for a stable logical index.
 * @param index Registry index, not the compact context array index.
 * @return Per-device state, or RT_NULL when all configured contexts are occupied.
 * @note Application registration is serialized; abandoned unregistered slots can be reused.
 */
static void *infrared_nec_get_context(rt_size_t index)
{
    rt_size_t slot;                                   /* Scan compact parser context slots. */
    rt_size_t free_slot = INFRARED_NEC_CONTEXT_COUNT; /* First unused/reclaimable NEC context. */
    for (slot = 0; slot < INFRARED_NEC_CONTEXT_COUNT; ++slot)
    {
        if (infrared_nec_slots[slot].used && infrared_nec_slots[slot].owner == index)
        {
            return &infrared_nec_slots[slot].context;
        }
        if (free_slot == INFRARED_NEC_CONTEXT_COUNT &&
            (!infrared_nec_slots[slot].used ||
             infrared_device_get(infrared_nec_slots[slot].owner) == RT_NULL))
        {
            free_slot = slot;
        }
    }
    if (free_slot == INFRARED_NEC_CONTEXT_COUNT)
    {
        return RT_NULL;
    }
    infrared_nec_slots[free_slot].owner = index;
    infrared_nec_slots[free_slot].used = RT_TRUE;
    rt_memset(&infrared_nec_slots[free_slot].context, 0,
              sizeof(infrared_nec_slots[free_slot].context));
    return &infrared_nec_slots[free_slot].context;
}

/**
 * @brief Original NEC leading 9-ms carrier burst; see nec_decoder_write().
 */
#define NEC_GUIDE_MARK_US 9000U
/**
 * @brief Original 4.5-ms gap selecting a normal 32-bit frame after the guide MARK.
 */
#define NEC_GUIDE_SPACE_US 4500U
/**
 * @brief Original 2.25-ms guide gap distinguishing a repeat frame.
 */
#define NEC_REPEAT_SPACE_US 2250U
/**
 * @brief Original per-bit carrier burst: 560 us before every decoded data SPACE.
 */
#define NEC_DATA_MARK_US 560U
/**
 * @brief Original logic-zero idle interval following a 560-us MARK.
 */
#define NEC_BIT_ZERO_US 560U
/**
 * @brief Original logic-one idle interval following a 560-us MARK.
 */
#define NEC_BIT_ONE_US 1690U
/**
 * @brief 43,580-us inter-frame idle gap copied from the original transmitter, not a universal NEC constant.
 */
#define NEC_FRAME_GAP_US 43580U
/**
 * @brief Original 4-byte NEC payload contains 32 MSB-first transmitted bits.
 */
#define NEC_DATA_BITS 32U
/**
 * @brief 2 guide + 32 pairs + 2 trailer intervals = 68 envelope entries.
 */
#define NEC_FRAME_PULSES 68U
/**
 * @brief 9-ms MARK, 2.25-ms SPACE, 560-us MARK and the original idle gap.
 */
#define NEC_REPEAT_PULSES 4U
/**
 * @brief Existing nec_decoder_write() clamps its application repeat count to eight.
 */
#define NEC_MAX_REPEAT 8U

/** @brief Shared pulse-distance timings, retaining the existing NEC receive tolerances. */
static const struct infrared_pd_timing nec_timing = {
    .guide_mark_us = NEC_GUIDE_MARK_US,
    .guide_mark_tol_us = 500U,
    .guide_space_us = NEC_GUIDE_SPACE_US,
    .guide_space_tol_us = 500U,
    .data_mark_us = NEC_DATA_MARK_US,
    .data_mark_tol_us = INFRARED_NEC_TOLERANCE_US,
    .zero_space_us = NEC_BIT_ZERO_US,
    .one_space_us = NEC_BIT_ONE_US,
    .data_space_tol_us = INFRARED_NEC_TOLERANCE_US,
    .trailer_mark_us = NEC_DATA_MARK_US,
    .trailer_mark_tol_us = INFRARED_NEC_TOLERANCE_US,
    .repeat_space_us = NEC_REPEAT_SPACE_US,
    .repeat_space_tol_us = 100U,
    .frame_gap_us = NEC_FRAME_GAP_US,
    .data_bits = NEC_DATA_BITS,
    .check_trailer_timing = RT_TRUE,
    .strict_bounds = RT_FALSE,
    .resync_on_invalid = RT_TRUE,
};

/**
 * @brief Reset the current NEC envelope while retaining the previous valid repeat command.
 * @param context Per-device NEC parser state.
 */
static void nec_reset(void *context)
{
    struct infrared_nec_context *ctx = (struct infrared_nec_context *)context;
    infrared_pd_reset(&ctx->pulse);
}

/**
 * @brief Consume normalized pulse-distance samples and validate standard NEC words.
 * @param context Per-device NEC state with independent repeat history.
 * @param pulses Packed normalized envelope segments.
 * @param pulse_count Number of available segments.
 * @param frames Destination complete-frame storage.
 * @param frame_capacity Number of available frame records.
 * @param consumed Number of pulse segments consumed.
 * @param produced Number of valid NEC frames emitted.
 * @return RT_EOK after consuming available pulses.
 */
static rt_err_t nec_decode(void *context, const infrared_pulse_t *pulses, rt_size_t pulse_count,
                           struct infrared_frame *frames, rt_size_t frame_capacity,
                           rt_size_t *consumed, rt_size_t *produced)
{
    struct infrared_nec_context *ctx = (struct infrared_nec_context *)context;
    rt_size_t index;        /* Input pulse index, stopped when output storage is full. */
    rt_size_t complete = 0; /* Valid NEC frame count emitted to the caller. */

    for (index = 0; index < pulse_count && complete < frame_capacity; index++)
    {
        rt_uint32_t word = 0; /* Completed 32-bit NEC word returned by the common envelope decoder. */
        enum infrared_pd_event event = infrared_pd_step(&ctx->pulse, &nec_timing, pulses[index],
                                                        ctx->has_last, &word);
        if (event == INFRARED_PD_FRAME)
        {
            rt_uint8_t addr = (rt_uint8_t)(word >> 24); /* Address and inverse. */
            rt_uint8_t addr_inv = (rt_uint8_t)(word >> 16);
            rt_uint8_t key = (rt_uint8_t)(word >> 8); /* Command and inverse. */
            rt_uint8_t key_inv = (rt_uint8_t)word;
            if ((rt_uint8_t)(addr ^ addr_inv) == 0xFFU &&
                (rt_uint8_t)(key ^ key_inv) == 0xFFU)
            {
                frames[complete].length = 3;
                frames[complete].data[0] = addr;
                frames[complete].data[1] = key;
                frames[complete].data[2] = 0;
                complete++;
                ctx->last_addr = addr;
                ctx->last_key = key;
                ctx->repeats = 0;
                ctx->has_last = RT_TRUE;
            }
            else
            {
                LOG_D("NEC inverse-byte check failed");
            }
        }
        else if (event == INFRARED_PD_REPEAT)
        {
            if (ctx->repeats < 255U)
            {
                ctx->repeats++;
            }
            frames[complete].length = 3;
            frames[complete].data[0] = ctx->last_addr;
            frames[complete].data[1] = ctx->last_key;
            frames[complete].data[2] = ctx->repeats;
            complete++;
        }
    }
    *consumed = index;
    *produced = complete;
    return RT_EOK;
}

/**
 * @brief Write one packed envelope segment to an already capacity-checked buffer.
 * @param buffer Output envelope array.
 * @param index Segment index.
 * @param level MARK or SPACE.
 * @param duration_us Segment duration in microseconds.
 */
static void nec_emit(infrared_pulse_t *buffer, rt_size_t index,
                     rt_uint32_t level, rt_uint32_t duration_us)
{
    buffer[index] = INFRARED_PULSE_PACK(level, duration_us);
}

/**
 * @brief Batch-encode whole NEC frames, stopping only at full-frame boundaries.
 * @param context Unused per-device NEC state.
 * @param frames Array of {address, key, repeat count} frames.
 * @param frame_count Number of complete input frames.
 * @param pulses Caller-provided packed envelope storage.
 * @param pulse_capacity Available packed pulse entries.
 * @param consumed Number of whole input frames encoded.
 * @param produced Number of generated envelope pulse records.
 * @return RT_EOK when at least one frame fits; -RT_EFULL if the first frame cannot fit.
 */
static rt_err_t nec_encode(void *context, const struct infrared_frame *frames,
                           rt_size_t frame_count, infrared_pulse_t *pulses,
                           rt_size_t pulse_capacity, rt_size_t *consumed, rt_size_t *produced)
{
    rt_size_t frame_index;
    rt_size_t cursor = 0;
    (void)context;
    *consumed = 0;
    *produced = 0;
    for (frame_index = 0; frame_index < frame_count; frame_index++)
    {
        rt_uint8_t repeat;
        rt_size_t required;
        rt_uint32_t command;
        rt_size_t index;
#if IR_CFG_CHECK_PARAMETERS
        if (frames[frame_index].length != 3)
        {
            return -RT_EINVAL;
        }
#endif /* IR_CFG_CHECK_PARAMETERS */
        repeat = frames[frame_index].data[2] > NEC_MAX_REPEAT ? NEC_MAX_REPEAT : frames[frame_index].data[2];
        required = NEC_FRAME_PULSES + NEC_REPEAT_PULSES * repeat;
        if (pulse_capacity - cursor < required)
        {
            break;
        }
        command = ((rt_uint32_t)frames[frame_index].data[0] << 24) |
                  ((rt_uint32_t)(rt_uint8_t)~frames[frame_index].data[0] << 16) |
                  ((rt_uint32_t)frames[frame_index].data[1] << 8) |
                  (rt_uint8_t)~frames[frame_index].data[1];
        cursor += infrared_pd_encode(&nec_timing, command, &pulses[cursor], pulse_capacity - cursor);
        for (index = 0; index < repeat; index++)
        {
            nec_emit(pulses, cursor++, INFRARED_MARK, NEC_GUIDE_MARK_US);
            nec_emit(pulses, cursor++, INFRARED_SPACE, NEC_REPEAT_SPACE_US);
            nec_emit(pulses, cursor++, INFRARED_MARK, NEC_DATA_MARK_US);
            nec_emit(pulses, cursor++, INFRARED_SPACE, NEC_FRAME_GAP_US);
        }
        (*consumed)++;
    }
    *produced = cursor;
    return *consumed ? RT_EOK : -RT_EFULL;
}

/**
 * @brief Shared immutable NEC operations; per-device receive state resides in context.
 */
const struct infrared_codec_ops infrared_nec_codec = {
    .context_for_index = infrared_nec_get_context,
    .reset = nec_reset,
    .decode = nec_decode,
    .encode = nec_encode
};
