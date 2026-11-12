/**
 * @file infrared_msh.c
 * @brief Framework diagnostic commands addressed by stable device indices.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#include "infrared_device.h"
#ifdef INFRARED_FRAMEWORK_NEC_CODEC
#include "nec_msh.h"
#endif /* INFRARED_FRAMEWORK_NEC_CODEC */
#include <finsh.h>

/**
 * @brief Parse a decimal device index, rejecting nondecimal names.
 * @param text NUL-terminated index string.
 * @param index Receives the parsed nonnegative index.
 * @return RT_EOK when valid, -RT_EINVAL otherwise.
 */
static rt_err_t ir_parse_index(const char *text, rt_size_t *index)
{
    rt_size_t value = 0;
    if (!text || !*text)
    {
        return -RT_EINVAL;
    }
    for (; *text; text++)
    {
        if (*text < '0' || *text > '9')
        {
            return -RT_EINVAL;
        }
        value = value * 10U + (rt_size_t)(*text - '0');
        if (value >= INFRARED_MAX_DEVICES)
        {
            return -RT_EINVAL;
        }
    }
    *index = value;
    return RT_EOK;
}

/**
 * @brief Print one protocol-neutral frame without assuming a particular codec.
 * @param frame Decoded complete frame record.
 */
static void ir_print_frame(const struct infrared_frame *frame)
{
    rt_size_t i;
    rt_kprintf("frame len=%u: ", (unsigned)frame->length);
    for (i = 0; i < frame->length && i < INFRARED_FRAME_MAX_BYTES; i++)
    {
        rt_kprintf("%02X", frame->data[i]);
    }
    rt_kprintf("\n");
}

/** @brief Print the user-visible commands, including the index-only address contract. */
static void ir_help(void)
{
    rt_kprintf("ir list | ir info <index> | ir help\n");
    rt_kprintf("ir open|close|poll|read <index>\n");
    rt_kprintf("ir flush <index> <now|defer>\n");
    rt_kprintf("ir tx <index> <hex-payload>\n");
    rt_kprintf("ir codec list | ir codec <name> read|tx ...\n");
}

/**
 * @brief Print a single registered device's status snapshot.
 * @param device Device returned by the framework registry.
 */
static void ir_print_info(struct infrared_device *device)
{
    struct infrared_device_info info;
    if (infrared_device_get_info(device, &info) != RT_EOK)
    {
        return;
    }
    rt_kprintf("[%u] %s open=%d rx=%s tx=%s queued=%u\n",
               (unsigned)info.index, info.name, info.opened, info.input.backend,
               info.output.backend,
               (unsigned)info.input.queued_frames);
}

/**
 * @brief Convert one ASCII hexadecimal nibble into its numerical value.
 * @param c ASCII hexadecimal character.
 * @return Nibble value, or -1 when invalid.
 */
static int ir_hex_digit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Run one indexed MSH diagnostic command without backend-specific access.
 * @param argc Number of arguments.
 * @param argv Argument vector beginning with "ir".
 */
static void ir(int argc, char **argv)
{
    rt_size_t index;
    struct infrared_device *device;
    rt_device_t rtdev;
    if (argc == 2 && rt_strcmp(argv[1], "list") == 0)
    {
        rt_kprintf("registered %u, capacity %u\n",
                   (unsigned)infrared_device_get_count(), (unsigned)INFRARED_MAX_DEVICES);
        for (index = 0; index < INFRARED_MAX_DEVICES; index++)
        {
            device = infrared_device_get(index);
            if (device)
            {
                ir_print_info(device);
            }
            else
            {
                rt_kprintf("[%u] unused\n", (unsigned)index);
            }
        }
        return;
    }
    if (argc == 2 && rt_strcmp(argv[1], "help") == 0)
    {
        ir_help();
        return;
    }
    if (argc >= 3 && rt_strcmp(argv[1], "codec") == 0)
    {
        if (argc == 3 && rt_strcmp(argv[2], "list") == 0)
        {
#ifdef INFRARED_FRAMEWORK_NEC_CODEC
            rt_kprintf("nec\n");
#endif /* INFRARED_FRAMEWORK_NEC_CODEC */
            return;
        }
#ifdef INFRARED_FRAMEWORK_NEC_CODEC
        if (rt_strcmp(argv[2], "nec") == 0)
        {
            infrared_nec_msh(argc - 3, argv + 3);
            return;
        }
#endif /* INFRARED_FRAMEWORK_NEC_CODEC */
        ir_help();
        return;
    }
    if (argc < 3 || ir_parse_index(argv[2], &index) != RT_EOK ||
        (device = infrared_device_get(index)) == RT_NULL)
    {
        ir_help();
        return;
    }
    rtdev = infrared_device_rt_device(device);
    if (rt_strcmp(argv[1], "info") == 0)
    {
        ir_print_info(device);
    }
    else if (rt_strcmp(argv[1], "open") == 0)
    {
        rt_kprintf("open: %d\n", rt_device_open(rtdev, RT_DEVICE_OFLAG_RDWR));
    }
    else if (rt_strcmp(argv[1], "close") == 0)
    {
        rt_err_t ret = rt_device_close(rtdev); /* Report this close result without a recovery retry. */
        rt_kprintf("close: %d\n", ret);
    }
    else if (rt_strcmp(argv[1], "poll") == 0)
    {
        rt_kprintf("poll: %d\n", infrared_device_poll(device));
    }
    else if (rt_strcmp(argv[1], "flush") == 0 && argc == 4)
    {
        rt_bool_t immediate;
        if (rt_strcmp(argv[3], "now") == 0)
        {
            immediate = RT_TRUE;
        }
        else if (rt_strcmp(argv[3], "defer") == 0)
        {
            immediate = RT_FALSE;
        }
        else
        {
            ir_help();
            return;
        }
        rt_kprintf("flush: %d\n", rt_device_control(rtdev, INFRARED_CTRL_RX_FLUSH, &immediate));
    }
    else if (rt_strcmp(argv[1], "read") == 0)
    {
        struct infrared_frame frame;
        rt_size_t count;
        do
        {
            count = infrared_device_read_frames(device, &frame, 1);
            if (count)
            {
                ir_print_frame(&frame);
            }
        } while (count);
    }
    else if (rt_strcmp(argv[1], "tx") == 0 && argc == 4)
    {
        struct infrared_frame frame;
        rt_size_t length = rt_strlen(argv[3]);
        rt_size_t i;
        if (!length || (length & 1U) || length > INFRARED_FRAME_MAX_BYTES * 2U)
        {
            rt_kprintf("invalid payload length\n");
            return;
        }
        rt_memset(&frame, 0, sizeof(frame));
        frame.length = (rt_uint8_t)(length / 2U);
        for (i = 0; i < frame.length; i++)
        {
            int hi = ir_hex_digit(argv[3][2U * i]);
            int lo = ir_hex_digit(argv[3][2U * i + 1U]);
            if (hi < 0 || lo < 0)
            {
                rt_kprintf("invalid hex payload\n");
                return;
            }
            frame.data[i] = (rt_uint8_t)((hi << 4) | lo);
        }
        rt_kprintf("sent %u frame(s)\n", (unsigned)infrared_device_write_frames(device, &frame, 1));
    }
    else
    {
        ir_help();
    }
}
MSH_CMD_EXPORT(ir, infrared multi - device diagnostics);
