/**
 * @file nec_codec.h
 * @brief Instance-based NEC batch codec and per-device parser context.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 *
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#ifndef NEC_CODEC_H
#define NEC_CODEC_H
#include "infrared_codec.h"
#include "pulse_distance.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**
 * @brief Decoder state for one infrared device, including prior valid repeat history.
 */
struct infrared_nec_context
{
    struct infrared_pd_decoder pulse; /**< Common pulse-distance envelope context. */
    rt_uint8_t last_addr; /**< Previous validated eight-bit address. */
    rt_uint8_t last_key;  /**< Previous validated eight-bit command. */
    rt_uint8_t repeats;   /**< Repeat frames since previous command. */
    rt_bool_t has_last;   /**< Previous validated command available for repeat frames. */
};

/**
 * @brief Stateless NEC batch codec; mutable state is infrared_nec_context.
 */
extern const struct infrared_codec_ops infrared_nec_codec;
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* NEC_CODEC_H */
