/**
 * @file infrared_example.c
 * @brief Optional compile-time configured devices and one event-driven worker for all slots.
 * @author wdfk-prog
 * @version 1.0
 * @date 2026-10-31
 * @attention Framework ownership is independent of this example and its initialization.
 * SPDX-License-Identifier: Apache-2.0
 * @par Change Logs:
 * Date           Author            Notes
 * 2026-10-31     wdfk-prog         first version
 */
#include "infrared_private.h"
#include "infrared_device_define.h"

#include "infrared_example.h"
#if defined(INFRARED_DEV0_CODEC_NEC) || defined(INFRARED_DEV1_CODEC_NEC) || \
    defined(INFRARED_DEV2_CODEC_NEC)
#include "nec_codec.h"
#endif /* Any built-in NEC example slot */

#define DBG_TAG "infrared.example"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#if defined(INFRARED_KCONFIG_DEVICE0)
IR_DEFINE_DEVICE(0, RT_NULL)
#endif /* INFRARED_KCONFIG_DEVICE0 */
#if INFRARED_KCONFIG_SLOT_COUNT > 1 && defined(INFRARED_KCONFIG_DEVICE1)
IR_DEFINE_DEVICE(1, RT_NULL)
#endif /* INFRARED_KCONFIG_SLOT_COUNT > 1 && defined(INFRARED_KCONFIG_DEVICE1) */
#if INFRARED_KCONFIG_SLOT_COUNT > 2 && defined(INFRARED_KCONFIG_DEVICE2)
IR_DEFINE_DEVICE(2, RT_NULL)
#endif /* INFRARED_KCONFIG_SLOT_COUNT > 2 && defined(INFRARED_KCONFIG_DEVICE2) */

/** @brief Slot-indexed immutable settings; disabled slots have no backing config. */
static const struct infrared_device_config * const infrared_example_configs[INFRARED_KCONFIG_SLOT_COUNT] = {
#if defined(INFRARED_KCONFIG_DEVICE0)
    [0] = &ir_cfg_device_0,
#else
    [0] = RT_NULL,
#endif /* INFRARED_KCONFIG_DEVICE0 */
#if INFRARED_KCONFIG_SLOT_COUNT > 1 && defined(INFRARED_KCONFIG_DEVICE1)
    [1] = &ir_cfg_device_1,
#endif /* INFRARED_KCONFIG_SLOT_COUNT > 1 && defined(INFRARED_KCONFIG_DEVICE1) */
#if INFRARED_KCONFIG_SLOT_COUNT > 2 && defined(INFRARED_KCONFIG_DEVICE2)
    [2] = &ir_cfg_device_2,
#endif /* INFRARED_KCONFIG_SLOT_COUNT > 2 && defined(INFRARED_KCONFIG_DEVICE2) */
};

/** @brief Built-in NEC operations, assigned only to example slots selecting NEC. */
static const struct infrared_codec_ops * const infrared_example_nec_codecs[INFRARED_KCONFIG_SLOT_COUNT] = {
#if defined(INFRARED_KCONFIG_DEVICE0) && defined(INFRARED_DEV0_CODEC_NEC)
    [0] = &infrared_nec_codec,
#else
    [0] = RT_NULL,
#endif /* INFRARED_KCONFIG_DEVICE0 && INFRARED_DEV0_CODEC_NEC */
#if INFRARED_KCONFIG_SLOT_COUNT > 1 && defined(INFRARED_KCONFIG_DEVICE1) && defined(INFRARED_DEV1_CODEC_NEC)
    [1] = &infrared_nec_codec,
#endif /* Device 1 selects NEC */
#if INFRARED_KCONFIG_SLOT_COUNT > 2 && defined(INFRARED_KCONFIG_DEVICE2) && defined(INFRARED_DEV2_CODEC_NEC)
    [2] = &infrared_nec_codec,
#endif /* Device 2 selects NEC */
};

/** @brief Per-slot codec selection and application-owned callback configuration. */
struct infrared_example_binding
{
    const struct infrared_codec_ops *codec; /**< Decoder/encoder selected for this device. */
    void *context;                          /**< Optional application-owned decoder context. */
    infrared_frame_callback_t callback;     /**< Thread-context frame handler. */
    void *user_data;                        /**< Application context for this device only. */
};

/** @brief Deferred slot bindings; devices are not registered until start(). */
static struct infrared_example_binding infrared_example_bindings[INFRARED_KCONFIG_SLOT_COUNT];

/** @brief Startup is one-shot because the RT-Thread device registry is permanent. */
static rt_bool_t infrared_example_started;

/**
 * @brief Bind a protocol operation table and callback to one configured example slot.
 * @param index Configured example index.
 * @param codec Codec table with a valid input decoder, if RX is enabled.
 * @param context Optional caller-owned parser context.
 * @param callback Application frame callback, or NULL to use read_frames().
 * @param user_data Per-device callback context.
 * @return RT_EOK, -RT_EBUSY if started, or -RT_EINVAL for invalid selection.
 */
rt_err_t infrared_example_configure(rt_size_t index,
                                    const struct infrared_codec_ops *codec, void *context,
                                    infrared_frame_callback_t callback, void *user_data)
{
    if (infrared_example_started)
    {
        return -RT_EBUSY;
    }
    if (index >= INFRARED_KCONFIG_SLOT_COUNT || !infrared_example_configs[index] || !codec)
    {
        return -RT_EINVAL;
    }
    infrared_example_bindings[index].codec = codec;
    infrared_example_bindings[index].context = context;
    infrared_example_bindings[index].callback = callback;
    infrared_example_bindings[index].user_data = callback ? user_data : RT_NULL;
    return RT_EOK;
}

#ifdef INFRARED_USING_THREAD_EXAMPLE
/** @brief Event object containing independent receive wakeup bits for configured instances. */
static struct rt_event infrared_example_event;

/** @brief One worker waits for any input device to signal available work. */
static rt_thread_t infrared_example_thread;

/**
 * @brief Raise the configured slot's event bit without blocking in an ISR.
 * @param device Receiver with new data, overflow or space in its frame queue.
 * @param user_data Pointer to config.index in the immutable per-slot configuration.
 */
static void infrared_example_notify(struct infrared_device *device, void *user_data)
{
    const rt_size_t index = *(const rt_size_t *)user_data; /* Immutable configured event bit. */
    (void)device;
    (void)rt_event_send(&infrared_example_event, (rt_uint32_t)(1UL << index));
}

/**
 * @brief Block on RX event bits and decode one pulse per poll for signaled devices.
 * @param parameter Unused RT-Thread thread argument.
 */
static void infrared_example_entry(void *parameter)
{
    rt_uint32_t events;
    rt_uint32_t pending = 0; /* Slots with remaining raw data after their processing quota. */
    rt_size_t index;
    rt_size_t quota;
    rt_err_t ret;
    (void)parameter;
    for (;;)
    {
        /* While local work remains, merge newly arrived events without blocking. */
        ret = rt_event_recv(&infrared_example_event,
                            (rt_uint32_t)((1UL << INFRARED_KCONFIG_SLOT_COUNT) - 1UL),
                            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                            pending ? 0 : RT_WAITING_FOREVER, &events);
        if (ret == RT_EOK)
        {
            pending |= events;
        }
        else if (!pending)
        {
            continue;
        }

        events = pending;
        pending = 0;
        /* A full pass services all notified slots before repeating a pending slot. */
        for (index = 0; index < INFRARED_KCONFIG_SLOT_COUNT; index++)
        {
            struct infrared_device *device;
            rt_uint32_t bit = (rt_uint32_t)(1UL << index); /* One configured device's event bit. */
            if (!(events & bit))
            {
                continue;
            }
            device = infrared_device_get(index);
            if (!device)
            {
                continue;
            }
            for (quota = 0; quota < INFRARED_THREAD_POLL_QUOTA; quota++)
            {
                ret = infrared_device_poll(device);
                if (ret != RT_EOK)
                {
                    if (ret != -RT_EEMPTY && ret != -RT_EFULL)
                    {
                        LOG_W("example: slot %u poll failed (%d)", (unsigned)index, ret);
                    }
                    break;
                }
            }
            if (quota == INFRARED_THREAD_POLL_QUOTA)
            {
                pending |= bit;
            }
        }
    }
}
#endif /* INFRARED_USING_THREAD_EXAMPLE */

/**
 * @brief Register configured devices and optionally start one event-driven RX worker.
 * @return RT_EOK on success or the first failed register, open or worker operation.
 */
rt_err_t infrared_example_start(void)
{
    rt_size_t index;    /* Example slot being registered or opened. */
    rt_err_t ret;       /* First error in the one-shot registration sequence. */
    rt_bool_t any = RT_FALSE; /* At least one configured slot must have a codec. */
    if (infrared_example_started)
    {
        return -RT_EBUSY;
    }
    for (index = 0; index < INFRARED_KCONFIG_SLOT_COUNT; index++)
    {
        const struct infrared_device_config *cfg = infrared_example_configs[index]; /* Slot's hardware selection. */
        if (cfg && (cfg->input.ops || cfg->output.ops) && !infrared_example_bindings[index].codec)
        {
            return -RT_EINVAL; /* Never silently omit an enabled CUSTOM device. */
        }
    }
#ifdef INFRARED_USING_THREAD_EXAMPLE
    rt_bool_t opened[INFRARED_KCONFIG_SLOT_COUNT] = { 0 };   /* Opened devices to close on failure. */
    rt_bool_t notified[INFRARED_KCONFIG_SLOT_COUNT] = { 0 }; /* Installed app RX hooks. */
    ret = rt_event_init(&infrared_example_event, "ir_evt", RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK)
    {
        LOG_E("example: event initialization failed (%d)", ret);
        return ret;
    }
#endif /* INFRARED_USING_THREAD_EXAMPLE */

    for (index = 0; index < INFRARED_KCONFIG_SLOT_COUNT; index++)
    {
        const struct infrared_device_config *cfg = infrared_example_configs[index];
        const struct infrared_example_binding *binding = &infrared_example_bindings[index];
        struct infrared_device_config selected; /* Local copy: codec is chosen at runtime. */
        if (!cfg || !binding->codec || (!cfg->input.ops && !cfg->output.ops))
        {
            continue;
        }
        any = RT_TRUE;
        selected = *cfg;
        selected.codec.ops = binding->codec;
        selected.codec.context = binding->context;
        ret = infrared_device_register(&selected);
        if (ret != RT_EOK)
        {
            LOG_E("example: slot %u registration failed (%d)", (unsigned)index, ret);
            goto fail;
        }
        if (cfg->input.ops)
        {
            ret = infrared_device_set_frame_callback(infrared_device_get(index),
                                                     binding->callback, binding->user_data);
            if (ret != RT_EOK)
            {
                goto fail;
            }
        }
    }
    if (!any)
    {
        ret = -RT_EINVAL;
        goto fail;
    }

#ifdef INFRARED_USING_THREAD_EXAMPLE
    for (index = 0; index < INFRARED_KCONFIG_SLOT_COUNT; index++)
    {
        const struct infrared_device_config *cfg = infrared_example_configs[index];
        struct infrared_device *device = infrared_device_get(index);
        if (!cfg || (!cfg->input.ops && !cfg->output.ops) || !device)
        {
            continue;
        }
        if (cfg->input.ops)
        {
            ret = infrared_device_set_rx_notify(device, infrared_example_notify,
                                                (void *)&cfg->index);
            if (ret != RT_EOK)
            {
                goto fail;
            }
            notified[index] = RT_TRUE;
        }
        ret = rt_device_open(infrared_device_rt_device(device), RT_DEVICE_OFLAG_RDWR);
        if (ret != RT_EOK)
        {
            LOG_E("example: slot %u open failed (%d)", (unsigned)index, ret);
            goto fail;
        }
        opened[index] = RT_TRUE;
    }
    infrared_example_thread = rt_thread_create("ir_event", infrared_example_entry, RT_NULL,
                                               INFRARED_THREAD_EXAMPLE_STACK,
                                               INFRARED_THREAD_EXAMPLE_PRIORITY, 10);
    if (!infrared_example_thread)
    {
        ret = -RT_ENOMEM;
        goto fail;
    }
    ret = rt_thread_startup(infrared_example_thread);
    if (ret != RT_EOK)
    {
        (void)rt_thread_delete(infrared_example_thread);
        infrared_example_thread = RT_NULL;
        goto fail;
    }
#endif /* INFRARED_USING_THREAD_EXAMPLE */
    infrared_example_started = RT_TRUE;
    LOG_I("example: registered configured infrared devices");
    return RT_EOK;
fail:
#ifdef INFRARED_USING_THREAD_EXAMPLE
    {
        rt_bool_t pending_rx = RT_FALSE; /* An IRQ producer may still own the Event lifetime. */
        for (index = 0; index < INFRARED_KCONFIG_SLOT_COUNT; index++)
        {
            struct infrared_device *device = infrared_device_get(index);
            rt_err_t close_ret; /* One logical close result, retained for logging. */
            if (!device)
            {
                continue;
            }
            if (notified[index])
            {
                /* Clear the app hook before an attempted close; a failed IRQ
                 * detach must not be followed by freeing its Event object. */
                (void)infrared_device_set_rx_notify(device, RT_NULL, RT_NULL);
            }
            if (opened[index])
            {
                close_ret = rt_device_close(infrared_device_rt_device(device));
                if (close_ret != RT_EOK)
                {
                    LOG_E("example: slot %u close failed (%d)", (unsigned)index, close_ret);
                }
            }
            if (device->rx.claimed_input)
            {
                pending_rx = RT_TRUE;
            }
        }
        if (!pending_rx)
        {
            (void)rt_event_detach(&infrared_example_event);
        }
        else
        {
            /* Failure is returned; BSP/application handles remaining hardware.
             * Preserve Event storage while any physical IRQ might still fire. */
            LOG_E("example: RX IRQ not fully released; retaining Event");
        }
    }
#endif /* INFRARED_USING_THREAD_EXAMPLE */
    return ret;
}
/**
 * @brief Populate NEC slots from the built-in codec and defer CUSTOM slots until bound.
 * @return RT_EOK or the first NEC binding or example start error.
 * @note All-NEC examples start automatically. Mixed NEC/CUSTOM examples wait
 *       for the application to call infrared_example_configure() and start().
 */
static int infrared_example_init(void)
{
    rt_size_t index;       /* Configured device slot. */
    rt_err_t ret;          /* Result of binding the slot's NEC decoder. */
    rt_bool_t pending = RT_FALSE; /* CUSTOM requires an application-supplied codec. */
    rt_bool_t any = RT_FALSE;     /* At least one active example device is configured. */

    for (index = 0; index < INFRARED_KCONFIG_SLOT_COUNT; index++)
    {
        const struct infrared_device_config *cfg = infrared_example_configs[index]; /* Hardware for this slot. */
        if (!cfg || (!cfg->input.ops && !cfg->output.ops))
        {
            continue;
        }
        any = RT_TRUE;
        if (infrared_example_nec_codecs[index])
        {
            ret = infrared_example_configure(index, infrared_example_nec_codecs[index],
                                             RT_NULL, RT_NULL, RT_NULL);
            if (ret != RT_EOK)
            {
                return ret;
            }
        }
        else if (!infrared_example_bindings[index].codec)
        {
            pending = RT_TRUE;
        }
    }
    if (!any || pending)
    {
        return RT_EOK;
    }
    return infrared_example_start();
}
INIT_APP_EXPORT(infrared_example_init);
