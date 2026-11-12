/**
 * @file nec_msh.c
 * @brief NEC-specific MSH read/transmit commands, dispatched by generic infrared MSH.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "nec_msh.h"
#include "nec_codec.h"
#include "infrared_device.h"

/** @brief Print the NEC extension's protocol-aware command forms. */
static void infrared_nec_help(void)
{
    rt_kprintf("ir codec nec read <index>\n");
    rt_kprintf("ir codec nec tx <index> <addr-hex> <cmd-hex> [repeats]\n");
}

/**
 * @brief Parse a short decimal value without integer overflow.
 * @param text ASCII decimal argument.
 * @param limit Maximum accepted integer value.
 * @param value Decoded integer on success.
 * @return RT_EOK or -RT_EINVAL on malformed input.
 */
static rt_err_t infrared_nec_decimal(const char *text, rt_size_t limit, rt_size_t *value)
{
    rt_size_t number = 0; /* Validated decimal accumulator. */
    if (!text || !*text)
    {
        return -RT_EINVAL;
    }
    for (; *text; ++text)
    {
        rt_size_t digit; /* Parsed ASCII numeral. */
        if (*text < '0' || *text > '9')
        {
            return -RT_EINVAL;
        }
        digit = (rt_size_t)(*text - '0');
        if (digit > limit || number > (limit - digit) / 10U)
        {
            return -RT_EINVAL;
        }
        number = number * 10U + digit;
    }
    *value = number;
    return RT_EOK;
}

/**
 * @brief Parse exactly two ASCII hexadecimal digits as an NEC payload byte.
 * @param text Two ASCII hexadecimal digits.
 * @param value Parsed byte on success.
 * @return RT_EOK or -RT_EINVAL.
 */
static rt_err_t infrared_nec_hex_byte(const char *text, rt_uint8_t *value)
{
    rt_uint8_t result = 0; /* Two-nibble output byte. */
    rt_size_t i;           /* Hex digit offset. */
    if (!text || rt_strlen(text) != 2U)
    {
        return -RT_EINVAL;
    }
    for (i = 0; i < 2; ++i)
    {
        rt_uint8_t nibble; /* Value of current hex character. */
        char c = text[i];  /* Raw ASCII input. */
        if (c >= '0' && c <= '9')
        {
            nibble = (rt_uint8_t)(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            nibble = (rt_uint8_t)(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            nibble = (rt_uint8_t)(c - 'A' + 10);
        }
        else
        {
            return -RT_EINVAL;
        }
        result = (rt_uint8_t)((result << 4) | nibble);
    }
    *value = result;
    return RT_EOK;
}

/**
 * @brief Dispatch NEC read and transmit operations after checking codec ownership.
 * @param argc Argument count without the three generic MSH command words.
 * @param argv NEC-specific arguments.
 */
void infrared_nec_msh(int argc, char **argv)
{
    rt_size_t index;                /* Stable registered device index. */
    struct infrared_device *device; /* Device bound to the NEC codec. */
    if (argc < 2 || infrared_nec_decimal(argv[1], INFRARED_MAX_DEVICES - 1U, &index) != RT_EOK)
    {
        infrared_nec_help();
        return;
    }
    device = infrared_device_get(index);
    if (!device || infrared_device_codec(device) != &infrared_nec_codec)
    {
        rt_kprintf("device %u is not registered with NEC\n", (unsigned)index);
        return;
    }
    if (argc == 2 && rt_strcmp(argv[0], "read") == 0)
    {
        struct infrared_frame frame; /* One decoded NEC command or repeat. */
        while (infrared_device_read_frames(device, &frame, 1U))
        {
            if (frame.length == 3U)
            {
                rt_kprintf("NEC addr=%02X command=%02X repeats=%u\n",
                           (unsigned)frame.data[0], (unsigned)frame.data[1],
                           (unsigned)frame.data[2]);
            }
            else
            {
                rt_kprintf("invalid NEC frame length: %u\n", (unsigned)frame.length);
            }
        }
    }
    else if (rt_strcmp(argv[0], "tx") == 0 && (argc == 4 || argc == 5))
    {
        struct infrared_frame frame; /* NEC addr, command and repeat count. */
        rt_size_t repeats = 0;       /* Optional NEC repeat frame count. */
        rt_memset(&frame, 0, sizeof(frame));
        frame.length = 3U;
        if (infrared_nec_hex_byte(argv[2], &frame.data[0]) != RT_EOK ||
            infrared_nec_hex_byte(argv[3], &frame.data[1]) != RT_EOK ||
            (argc == 5 && infrared_nec_decimal(argv[4], 8U, &repeats) != RT_EOK))
        {
            infrared_nec_help();
            return;
        }
        frame.data[2] = (rt_uint8_t)repeats;
        rt_kprintf("NEC frames sent: %u\n",
                   (unsigned)infrared_device_write_frames(device, &frame, 1U));
    }
    else
    {
        infrared_nec_help();
    }
}
