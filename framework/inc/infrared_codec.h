/**
 * @file infrared_codec.h
 * @brief Compact protocol frames and per-device batch codec operations.
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
#ifndef INFRARED_CODEC_H
#define INFRARED_CODEC_H

#include <rtthread.h>
#include "infrared_framework_config.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * @brief 32-bit normalized pulse: high nibble level, low 28 bits duration in us.
 */
typedef rt_uint32_t infrared_pulse_t;

/**
 * @brief Original raw format's carrier-on marker (0xA in bits 31..28).
 * @note These are historical wire-envelope tags, not GPIO logic levels.
 */
#define INFRARED_MARK 0xAU
/**
 * @brief Carrier-off (SPACE) marker encoded as 0xB in the high nibble.
 * @note This software tag matches the historical IDLE_SIGNAL value; it is
 *       not a physical GPIO logic level.
 */
#define INFRARED_SPACE 0xBU
/**
 * @brief Maximum 28-bit microsecond duration.
 */
#define INFRARED_PULSE_MAX_US 0x0FFFFFFFU
/**
 * @brief Construct one unambiguous packed envelope segment.
 */
#define INFRARED_PULSE_PACK(level, duration) \
    ((((rt_uint32_t)(level)) << 28) | ((rt_uint32_t)(duration) & INFRARED_PULSE_MAX_US))
/**
 * @brief Extract the envelope level from a packed segment.
 */
#define INFRARED_PULSE_LEVEL(pulse) (((rt_uint32_t)(pulse) >> 28) & 0xFU)
/**
 * @brief Extract a microsecond duration from a packed segment.
 */
#define INFRARED_PULSE_DURATION(pulse) ((rt_uint32_t)(pulse) & INFRARED_PULSE_MAX_US)

/** @brief One logical frame record before any ringbuffer alignment padding. */
#define INFRARED_FRAME_RAW_SIZE (1U + INFRARED_FRAME_MAX_BYTES)
/**
 * @brief Padding bytes required by the RT-Thread ringbuffer alignment rule.
 * @note RT-Thread rounds buffer capacity down to RT_ALIGN_SIZE; each frame record
 *       must remain whole even when only one frame is reserved.
 */
#define INFRARED_FRAME_PADDING \
    (RT_ALIGN(INFRARED_FRAME_RAW_SIZE, (RT_ALIGN_SIZE + 0U)) - INFRARED_FRAME_RAW_SIZE)

/**
 * @brief Protocol-neutral complete frame; the payload byte order is codec-specific.
 */
struct infrared_frame
{
    rt_uint8_t length;                         /**< Valid payload byte count. */
    rt_uint8_t data[INFRARED_FRAME_MAX_BYTES]; /**< Full-frame payload bytes. */
#if INFRARED_FRAME_PADDING
    rt_uint8_t reserved[INFRARED_FRAME_PADDING]; /**< Align each FIFO record for RT-Thread ringbuffer. */
#endif                                           /* INFRARED_FRAME_PADDING */
};

/**
 * @brief Protocol operations; each receiver owns independent streaming parser state.
 */
struct infrared_codec_ops
{
    /**
     * @brief Optionally resolve persistent codec storage owned by its implementation.
     * @param index Fixed infrared device registry index.
     * @return Per-device state, or RT_NULL if a caller-supplied context is required.
     */
    void *(*context_for_index)(rt_size_t index);

    /**
     * @brief reset: Clear an incomplete receive frame without losing optional repeat history.
     * @param context Per-device codec state.
     */
    void (*reset)(void *context);

    /**
     * @brief decode: Consume the supplied pulse(s) into persistent decoder state.
     * @param context Per-device parser state that persists between calls.
     * @param pulses Array of packed microsecond pulse segments.
     * @param pulse_count Available pulse entries (framework polling supplies one per call).
     * @param frames Output array for complete protocol frames.
     * @param frame_capacity Maximum number of output records.
     * @param consumed Number of input pulses consumed (must equal pulse_count for framework polling).
     * @param produced Number of complete output frames written.
     * @return RT_EOK after consuming all supplied pulses, or a negative RT-Thread error.
     */
    rt_err_t (*decode)(void *context, const infrared_pulse_t *pulses, rt_size_t pulse_count,
                       struct infrared_frame *frames, rt_size_t frame_capacity,
                       rt_size_t *consumed, rt_size_t *produced);

    /**
     * @brief encode: Encode whole frames into a buffer supplied by the output backend.
     * @param context Per-device encoder context.
     * @param frames Array of complete protocol frames.
     * @param frame_count Number of input protocol frames.
     * @param pulses Destination envelope buffer, in 32-bit packed format.
     * @param pulse_capacity Number of available pulse entries.
     * @param consumed Number of whole input frames encoded.
     * @param produced Number of output pulse segments generated.
     * @return RT_EOK after at least one complete frame, -RT_EFULL if no complete frame fits,
     *         or another negative RT-Thread error.
     */
    rt_err_t (*encode)(void *context, const struct infrared_frame *frames, rt_size_t frame_count,
                       infrared_pulse_t *pulses, rt_size_t pulse_capacity,
                       rt_size_t *consumed, rt_size_t *produced);
};

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* INFRARED_CODEC_H */
