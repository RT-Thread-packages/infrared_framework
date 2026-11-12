/**
 * @file infrared_device.c
 * @brief Register infrared logical devices and process nonblocking receive batches.
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
#include "infrared_private.h"
#include "ipc/ringbuffer.h"

#define DBG_TAG "infrared.device"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/** @brief Reject configurations that would truncate the RT-Thread ringbuffer length. */
typedef char infrared_frame_buffer_size_check[INFRARED_FRAME_CAPACITY * sizeof(struct infrared_frame) <= 0x7FFFU ? 1 : -1];

/** @brief Sole registry and lifetime storage; disabled sparse slots remain unregistered. */
static struct infrared_device infrared_devices[INFRARED_MAX_DEVICES];

#if IR_CFG_RX_POOL_ENABLED
/** @brief Compact pool of complete-frame buffers, allocated only for configured receivers. */
static struct infrared_frame infrared_frame_storage[IR_CONFIGURED_RX_COUNT][INFRARED_FRAME_CAPACITY];
/** @brief Open-lifetime ownership flags for the complete-frame buffers. */
static rt_bool_t infrared_frame_used[IR_CONFIGURED_RX_COUNT];
#endif /* IR_CFG_RX_POOL_ENABLED */

/**
 * @brief Reserve a complete-frame buffer for one opened receiver.
 * @param ir Receiver whose lifetime owns the buffer.
 * @return RT_EOK or -RT_ENOMEM when the configured pool is exhausted.
 */
static rt_err_t infrared_frame_acquire(struct infrared_device *ir)
{
#if IR_CFG_RX_POOL_ENABLED
    rt_size_t index;                             /* Candidate complete-frame pool slot. */
    rt_base_t level = rt_hw_interrupt_disable(); /* Protect pool ownership. */
    for (index = 0; index < IR_CONFIGURED_RX_COUNT; index++)
    {
        if (!infrared_frame_used[index])
        {
            infrared_frame_used[index] = RT_TRUE;
            ir->rx.frame_storage = (rt_uint8_t *)infrared_frame_storage[index];
            ir->rx.frame_pool_index = index;
            rt_hw_interrupt_enable(level);
            rt_ringbuffer_init(&ir->rx.frame_queue, ir->rx.frame_storage,
                               sizeof(infrared_frame_storage[0]));
            return RT_EOK;
        }
    }
    rt_hw_interrupt_enable(level);
#endif /* IR_CFG_RX_POOL_ENABLED */
    return -RT_ENOMEM;
}

/**
 * @brief Return the frame buffer to the pool only after the receiver has stopped.
 * @param ir Receiver which previously reserved the buffer.
 */
static void infrared_frame_release(struct infrared_device *ir)
{
#if IR_CFG_RX_POOL_ENABLED
    if (ir->rx.frame_storage)
    {
        rt_base_t level = rt_hw_interrupt_disable(); /* Protect pool ownership. */
        infrared_frame_used[ir->rx.frame_pool_index] = RT_FALSE;
        ir->rx.frame_storage = RT_NULL;
        rt_hw_interrupt_enable(level);
    }
#else
    (void)ir;
#endif /* IR_CFG_RX_POOL_ENABLED */
}

/** @brief Registered instance count; it does not influence the stable slot indices. */
static rt_size_t infrared_device_count;

/**
 * @brief Return a device by its fixed index without searching for registration order.
 * @param index Explicit application-selected device slot.
 * @return Registered instance or RT_NULL when the slot is empty.
 */
struct infrared_device *infrared_device_get(rt_size_t index)
{
#if IR_CFG_CHECK_PARAMETERS
    if (index >= INFRARED_MAX_DEVICES)
    {
        return RT_NULL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    if (!infrared_devices[index].registered)
    {
        return RT_NULL;
    }
    return &infrared_devices[index];
}

/**
 * @brief Return the registered protocol implementation for MSH codec dispatch.
 * @param device Registered logical device.
 * @return Codec operation table or RT_NULL when absent.
 */
const struct infrared_codec_ops *infrared_device_codec(struct infrared_device *device)
{
    return device && device->registered ? device->config.codec.ops : RT_NULL;
}

/**
 * @brief Return the number of registered instances without compressing sparse slots.
 * @return Number of successful device registrations.
 */
rt_size_t infrared_device_get_count(void)
{
    return infrared_device_count;
}

/**
 * @brief Return the corresponding embedded RT-Thread device handle.
 * @param device Framework-owned infrared device.
 * @return RT-Thread device handle, or RT_NULL for invalid input.
 */
rt_device_t infrared_device_rt_device(struct infrared_device *device)
{
#if IR_CFG_CHECK_PARAMETERS
    if (!device)
    {
        return RT_NULL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    return device->registered ? &device->parent : RT_NULL;
}

#ifdef INFRARED_USING_DIAGNOSTICS
/**
 * @brief Take a public status snapshot without exposing hardware-specific state.
 * @param device Registered infrared instance.
 * @param info Destination snapshot structure.
 * @return RT_EOK or -RT_EINVAL.
 */
rt_err_t infrared_device_get_info(struct infrared_device *device,
                                  struct infrared_device_info *info)
{
    rt_base_t level;
#if IR_CFG_CHECK_PARAMETERS
    if (!device || !device->registered || !info)
    {
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    level = rt_hw_interrupt_disable();
    info->name = device->config.name;
    info->index = device->registry_index;
    info->input.configured = device->config.input.ops != RT_NULL;
    info->output.configured = device->config.output.ops != RT_NULL;
    info->input.backend = info->input.configured ? device->config.input.ops->name : "none";
    info->output.backend = info->output.configured ? device->config.output.ops->name : "none";
    info->opened = device->opened;
    info->input.queued_frames = device->rx.frame_storage ? rt_ringbuffer_data_len(&device->rx.frame_queue) / sizeof(struct infrared_frame) : 0;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

#endif /* INFRARED_USING_DIAGNOSTICS */

/**
 * @brief Claim or release a callback-producing physical timer or capture device.
 * @param ir Owner of the physical resource.
 * @param physical BSP-registered RT-Thread device, or RT_NULL to release the claim.
 * @param route IR_RESOURCE_INPUT for RX, IR_RESOURCE_OUTPUT for TX.
 * @return RT_EOK on success or -RT_EBUSY if another instance owns it.
 * @note All claims live in infrared_device, not in adapter-specific tables.
 */
rt_err_t infrared_device_bind_physical(struct infrared_device *ir, rt_device_t physical,
                                       enum infrared_resource_route route)
{
    /* Scan other owners atomically with publishing this instance's binding. */
    rt_size_t index;
    rt_base_t level = rt_hw_interrupt_disable();
    for (index = 0; index < INFRARED_MAX_DEVICES && physical != RT_NULL; ++index)
    {
        const struct infrared_device *other = &infrared_devices[index];
        if (!other->registered || other == ir)
        {
            continue;
        }
        if (other->rx.claimed_input == physical || other->tx.claimed_output == physical)
        {
            rt_hw_interrupt_enable(level);
            return -RT_EBUSY;
        }
    }
    if (physical != RT_NULL &&
        ((route == IR_RESOURCE_INPUT && ir->tx.claimed_output == physical) ||
         (route == IR_RESOURCE_OUTPUT && ir->rx.claimed_input == physical)))
    {
        rt_hw_interrupt_enable(level);
        return -RT_EBUSY;
    }
    if (route == IR_RESOURCE_INPUT)
    {
        ir->rx.claimed_input = physical;
    }
    else
    {
        ir->tx.claimed_output = physical;
    }
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

/**
 * @brief Atomically reserve a physical device and its numbered subchannel.
 * @param ir Logical device which owns the subchannel.
 * @param physical Any RT-Thread physical device, or RT_NULL to release a claim.
 * @param channel Subchannel identifier understood by the corresponding adapter.
 * @param period_ns Effective carrier period shared by channels of the same PWM timer.
 * @return RT_EOK, or -RT_EBUSY when another infrared instance conflicts.
 */
rt_err_t infrared_device_bind_channel(struct infrared_device *ir, rt_device_t physical,
                                      rt_int32_t channel, rt_uint32_t period_ns)
{
    /* Protect device/subchannel ownership against concurrent device opens. */
    rt_size_t index;
    rt_base_t level = rt_hw_interrupt_disable();
    for (index = 0; index < INFRARED_MAX_DEVICES && physical != RT_NULL; ++index)
    {
        const struct infrared_device *other = &infrared_devices[index];
        if (other->registered && other != ir && other->tx.claimed_channel == physical &&
            (other->tx.claimed_channel_index == channel ||
             other->tx.claimed_channel_period_ns != period_ns))
        {
            rt_hw_interrupt_enable(level);
            return -RT_EBUSY;
        }
    }
    ir->tx.claimed_channel = physical;
    ir->tx.claimed_channel_index = channel;
    ir->tx.claimed_channel_period_ns = physical != RT_NULL ? period_ns : 0U;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

/**
 * @brief Attach one application wakeup hook with a caller-managed ISR lifetime.
 * @param device Registered receiver.
 * @param notify ISR-safe hook, or NULL to unregister.
 * @param user_data App-owned hook state valid for callback lifetime.
 * @return RT_EOK or -RT_EINVAL for invalid arguments when checks are enabled.
 */
rt_err_t infrared_device_set_rx_notify(struct infrared_device *device,
                                       infrared_rx_notify_t notify, void *user_data)
{
    rt_base_t level;
#if IR_CFG_CHECK_PARAMETERS
    if (!device || !device->registered || !device->config.input.ops)
    {
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    level = rt_hw_interrupt_disable();
    device->rx.notify = notify;
    device->rx.notify_data = notify ? user_data : RT_NULL;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

/**
 * @brief Assign one complete-frame consumer and its context to a device.
 * @param device Registered receiver (may already be opened).
 * @param callback Frame handler, or NULL to retain frame FIFO operation.
 * @param user_data Caller-owned context valid through any in-flight callback.
 * @return RT_EOK, or -RT_EINVAL for an invalid receiver.
 */
rt_err_t infrared_device_set_frame_callback(struct infrared_device *device,
                                            infrared_frame_callback_t callback,
                                            void *user_data)
{
    if (!device || !device->registered || !device->config.input.ops)
    {
        return -RT_EINVAL;
    }
    /* Publish the handler and context together for the poll thread. */
    rt_enter_critical();
    device->rx.frame_callback = callback;
    device->rx.frame_user_data = callback ? user_data : RT_NULL;
    rt_exit_critical();
    return RT_EOK;
}

/**
 * @brief Notify a registered receive poll owner without decoding inside ISR.
 * @param ir Input instance whose work state changed.
 */
void infrared_device_notify_rx(struct infrared_device *ir)
{
    infrared_rx_notify_t notify = ir->rx.notify; /* Installed application wakeup hook. */
    if (notify)
    {
        notify(ir, ir->rx.notify_data);
    }
}

/**
 * @brief Mark a pulse discontinuity for resynchronization by the parser.
 * @param device Device whose input queue lost one or more pulses.
 * @note ISR-safe; this function does not parse a protocol or allocate memory.
 */
void infrared_report_overflow(struct infrared_device *device)
{
    device->rx.overflow = RT_TRUE;
    infrared_device_notify_rx(device);
}

/**
 * @brief Enqueue a complete array of fixed-size protocol frames into the RT-Thread ringbuffer.
 * @param device Device whose framework-owned static frame pool backs its ringbuffer.
 * @param frames Source array of decoded records.
 * @param count Maximum number of records offered.
 * @return Number of complete frames stored without truncation.
 */
static rt_size_t infrared_frame_put(struct infrared_device *device,
                                    const struct infrared_frame *frames, rt_size_t count)
{
    rt_size_t capacity;
    rt_size_t bytes;
    rt_enter_critical();
    capacity = rt_ringbuffer_space_len(&device->rx.frame_queue) / sizeof(struct infrared_frame);
    if (count > capacity)
    {
        count = capacity;
    }
    bytes = count * sizeof(struct infrared_frame);
    if (bytes)
    {
        bytes = rt_ringbuffer_put(&device->rx.frame_queue, (const rt_uint8_t *)frames, (rt_uint16_t)bytes);
    }
    rt_exit_critical();
    return bytes / sizeof(struct infrared_frame);
}

/**
 * @brief Read multiple complete frames using the same RT-Thread ringbuffer API as physical drivers.
 * @param device Open infrared receiver with an initialized frame queue.
 * @param frames Destination array of frame records.
 * @param count Maximum number of records that the destination can hold.
 * @return Number of complete frames copied into frames.
 */
static rt_size_t infrared_frame_get(struct infrared_device *device,
                                    struct infrared_frame *frames, rt_size_t count)
{
    rt_size_t available;
    rt_size_t bytes;
    rt_enter_critical();
    available = rt_ringbuffer_data_len(&device->rx.frame_queue) / sizeof(struct infrared_frame);
    if (count > available)
    {
        count = available;
    }
    bytes = count * sizeof(struct infrared_frame);
    if (bytes)
    {
        bytes = rt_ringbuffer_get(&device->rx.frame_queue, (rt_uint8_t *)frames, (rt_uint16_t)bytes);
    }
    rt_exit_critical();
    /* Releasing frame space lets the receive worker resume draining raw samples. */
    if (bytes && device->config.input.ops)
    {
        infrared_device_notify_rx(device);
    }
    return bytes / sizeof(struct infrared_frame);
}

/**
 * @brief Clear raw samples and codec state; optionally discard completed frames too.
 * @param device Receiver whose polling and control calls are serialized by the application.
 * @param clear_frames RT_TRUE for an explicit full RX flush, RT_FALSE for overflow recovery.
 * @return RT_EOK only after the hardware queue accepts the flush request.
 */
static rt_err_t infrared_flush_receive(struct infrared_device *device, rt_bool_t clear_frames)
{
    rt_err_t ret = device->config.input.ops->io.rx.flush(device);
    if (ret != RT_EOK)
    {
        return ret;
    }
    device->config.codec.ops->reset(device->config.codec.context);
    if (clear_frames && device->rx.frame_storage)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        rt_ringbuffer_reset(&device->rx.frame_queue);
        rt_hw_interrupt_enable(level);
    }
    return RT_EOK;
}

/**
 * @brief Decode one available pulse from the input adapter in the calling thread.
 * @param device Open receiver with exclusive parser ownership held by its caller.
 * @return RT_EOK when one pulse was consumed, -RT_EEMPTY if none is ready, or an error.
 * @note The caller must serialize polling, disabling RX and closing this device.
 */
rt_err_t infrared_device_poll(struct infrared_device *device)
{
    infrared_pulse_t pulse;      /* Exactly one raw sample is decoded per invocation. */
    struct infrared_frame frame; /* At most one complete frame from a sample. */
    rt_size_t consumed = 0;      /* Pulses accepted by the stateful decoder. */
    rt_size_t produced = 0;      /* Complete frames emitted from this sample. */
    rt_err_t ret;                /* Error from queue or codec processing. */
    rt_bool_t overflow;          /* Snapshot of discontinuity from ISR. */
    rt_bool_t flush_pending;     /* Explicit full flush requested by device control. */
    rt_base_t level;             /* Short interrupt gate for receive flags. */
    infrared_frame_callback_t callback; /* Atomic snapshot of the complete-frame consumer. */
    void *frame_user_data;       /* Matching callback context for this poll call. */
#if IR_CFG_CHECK_PARAMETERS
    if (!device || !device->registered || !device->opened || !device->config.input.ops ||
        rt_interrupt_get_nest() != 0)
    {
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    level = rt_hw_interrupt_disable();
    overflow = device->rx.overflow;
    flush_pending = device->rx.flush_pending;
    device->rx.overflow = RT_FALSE;
    device->rx.flush_pending = RT_FALSE;
    rt_hw_interrupt_enable(level);
    if (flush_pending || overflow)
    {
        ret = infrared_flush_receive(device, flush_pending);
        if (ret != RT_EOK)
        {
            if (!flush_pending && ret == -RT_ENOSYS)
            {
                /* Preserve the old best-effort overflow recovery on BSPs without clear control. */
                device->config.codec.ops->reset(device->config.codec.context);
                LOG_W("%s: raw queue flush unsupported after overflow", device->config.name);
            }
            else
            {
                /* An explicit user flush remains pending if the BSP cannot clear samples. */
                level = rt_hw_interrupt_disable();
                device->rx.flush_pending |= flush_pending;
                device->rx.overflow |= overflow;
                rt_hw_interrupt_enable(level);
                return ret;
            }
        }
        else if (overflow && !flush_pending)
        {
            LOG_D("%s: input pulse continuity lost", device->config.name);
        }
    }
    rt_enter_critical();
    callback = device->rx.frame_callback;
    frame_user_data = device->rx.frame_user_data;
    ret = callback ||
          rt_ringbuffer_space_len(&device->rx.frame_queue) >= sizeof(struct infrared_frame) ? RT_EOK : -RT_EFULL;
    rt_exit_critical();
    if (ret != RT_EOK)
    {
        return ret;
    }
    if (device->config.input.ops->io.rx.read(device, &pulse, 1) != 1)
    {
        return -RT_EEMPTY;
    }

    rt_memset(&frame, 0, sizeof(frame));
    ret = device->config.codec.ops->decode(device->config.codec.context, &pulse, 1,
                                           &frame, 1, &consumed, &produced);
    if (ret != RT_EOK || consumed != 1 || produced > 1)
    {
        device->config.codec.ops->reset(device->config.codec.context);
        LOG_E("%s: codec failed to consume a pulse (%d)", device->config.name, ret);
        return ret != RT_EOK ? ret : -RT_ERROR;
    }
    if (produced)
    {
        if (callback)
        {
            return callback(device, &frame, frame_user_data);
        }
        else
        {
            rt_size_t inserted = infrared_frame_put(device, &frame, 1); /* FIFO mode only. */
            if (inserted && device->parent.rx_indicate)
            {
                device->parent.rx_indicate(&device->parent, inserted);
            }
        }
    }
    return RT_EOK;
}

/**
 * @brief Start or stop input while keeping the buffer and codec state synchronized.
 * @param device Infrared device whose input hardware is already opened.
 * @param enable RT_TRUE enables reception; RT_FALSE disables and clears partial input.
 * @return RT_EOK or the physical adapter's error code.
 */
rt_err_t infrared_device_receive_enable(struct infrared_device *device, rt_bool_t enable)
{
    rt_err_t ret;
    rt_err_t flush_ret; /* Result of the single raw-queue clearing attempt. */
#if IR_CFG_CHECK_PARAMETERS
    if (!device || !device->registered || !device->opened || !device->config.input.ops ||
        rt_interrupt_get_nest() != 0)
    {
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    if (!device->config.input.ops)
    {
        return -RT_ENOSYS;
    }
    if (enable)
    {
        device->rx.overflow = RT_FALSE;
        device->rx.flush_pending = RT_FALSE;
        device->config.codec.ops->reset(device->config.codec.context);
        flush_ret = device->config.input.ops->io.rx.flush(device);
        if (flush_ret == -RT_ENOSYS)
        {
            /* Optional pre-start clearing must not prevent Capture BSPs from opening. */
            LOG_W("%s: RX pre-start flush unsupported", device->config.name);
        }
        else if (flush_ret != RT_EOK)
        {
            LOG_E("%s: RX pre-start flush failed (%d)", device->config.name, flush_ret);
            return flush_ret;
        }
        ret = device->config.input.ops->start(device);
        if (ret != RT_EOK)
        {
            LOG_E("%s: RX start failed (%d)", device->config.name, ret);
        }
    }
    else
    {
        ret = device->config.input.ops->stop(device);
        if (ret != RT_EOK)
        {
            LOG_E("%s: RX stop failed (%d)", device->config.name, ret);
        }
        device->rx.flush_pending = RT_FALSE;
        flush_ret = device->config.input.ops->io.rx.flush(device);
        if (flush_ret != RT_EOK)
        {
            LOG_E("%s: RX flush after stop failed (%d)", device->config.name, flush_ret);
            if (ret == RT_EOK)
            {
                ret = flush_ret;
            }
        }
        device->config.codec.ops->reset(device->config.codec.context);
    }
    return ret;
}

/**
 * @brief Run one complete encoded envelope batch, with TX concurrency owned by caller.
 * @param device Output instance holding stable TX pulse memory until completion.
 * @return RT_EOK after physical output completes or the timer/timeout error.
 */
static rt_err_t infrared_send_batch(struct infrared_device *device)
{
    rt_err_t ret;
    /* Backend-owned user_data begins with the shared TX buffer contract. */
    struct infrared_tx_buffer *buffer = (struct infrared_tx_buffer *)device->tx.user_data;
    /* Completion uses a signed timeout; keep the package zero-means-forever convention. */
    rt_int32_t timeout = device->config.output.timeout_ticks ? device->config.output.timeout_ticks : RT_WAITING_FOREVER;
    rt_completion_init(&device->tx.done);
    buffer->result = RT_EOK;
    ret = device->config.output.ops->start(device);
    if (ret == RT_EOK)
    {
        ret = rt_completion_wait(&device->tx.done, timeout);
        if (ret == RT_EOK)
        {
            ret = buffer->result;
        }
    }
    {
        rt_err_t stop_ret = device->config.output.ops->stop(device);
        if (stop_ret != RT_EOK)
        {
            LOG_E("%s: TX stop failed (%d)", device->config.name, stop_ret);
        }
        if (ret == RT_EOK)
        {
            ret = stop_ret;
        }
    }
    return ret;
}

/**
 * @brief Encode and send an array of complete frames in bounded, whole-frame batches.
 * @param device Open transmitter with backend-owned stable packed pulse storage.
 * @param frames Array of whole protocol frames to transmit.
 * @param count Number of source frames, not bytes.
 * @return Number of completely transmitted frames; callers must serialize writers.
 */
rt_size_t infrared_device_write_frames(struct infrared_device *device,
                                       const struct infrared_frame *frames, rt_size_t count)
{
    rt_size_t completed = 0;
    rt_err_t ret = RT_EOK;
    struct infrared_tx_buffer *buffer; /* View of backend-owned TX storage. */
#if IR_CFG_CHECK_PARAMETERS
    if (!device || !frames || !device->registered || !device->opened ||
        !device->config.output.ops || !device->config.codec.ops->encode ||
        rt_interrupt_get_nest() != 0)
    {
        return 0;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    if (!count || !device->config.output.ops || !device->opened)
    {
        return 0;
    }
    buffer = (struct infrared_tx_buffer *)device->tx.user_data;
    if (!buffer)
    {
        return 0;
    }
    /* The application serializes writers and close(); this buffer is not reentrant. */
    while (completed < count)
    {
        rt_size_t encoded_frames = 0;
        rt_size_t pulses = 0;
        ret = device->config.codec.ops->encode(device->config.codec.context,
                                               &frames[completed], count - completed,
                                               buffer->pulses, buffer->capacity,
                                               &encoded_frames, &pulses);
        if (ret != RT_EOK || !encoded_frames || !pulses ||
            pulses > buffer->capacity || encoded_frames > count - completed)
        {
            LOG_E("%s: TX encode failed (%d)", device->config.name,
                  ret != RT_EOK ? ret : -RT_ERROR);
            break;
        }
        buffer->count = pulses;
        ret = infrared_send_batch(device);
        if (ret != RT_EOK)
        {
            LOG_W("%s: output failed (%d)", device->config.name, ret);
            break;
        }
        completed += encoded_frames;
    }
    if (completed && device->parent.tx_complete)
    {
        device->parent.tx_complete(&device->parent, (void *)frames);
    }
    return completed;
}

/**
 * @brief Acquire this infrared instance's configured physical resources.
 * @param dev Address of infrared_device.parent (the first structure member).
 * @param flags RT-Thread open flags forwarded to the logical device.
 * @return RT_EOK or the error returned by a physical adapter.
 * @note Open failures receive one best-effort rollback; the caller handles physical faults.
 */
static rt_err_t infrared_rt_open(rt_device_t dev, rt_uint16_t flags)
{
    struct infrared_device *ir = (struct infrared_device *)dev;
    rt_err_t ret = RT_EOK;
    rt_err_t close_ret; /* One rollback attempt; preserve the original open error. */
    (void)flags;
    if (ir->opened)
    {
        return RT_EOK;
    }
    /* A failed physical teardown is a fault for the caller/BSP, not an automatic
     * recovery state: do not let another open reuse a still-claimed resource. */
    if (ir->rx.claimed_input || ir->rx.frame_storage || ir->tx.claimed_output ||
        ir->tx.claimed_channel || ir->tx.user_data)
    {
        LOG_E("%s: outstanding hardware claim prevents open", ir->config.name);
        return -RT_EBUSY;
    }
    if (ir->config.input.ops)
    {
        ret = infrared_frame_acquire(ir);
        if (ret != RT_EOK)
        {
            LOG_E("%s: no RX frame buffer (%d)", ir->config.name, ret);
            return ret;
        }
        ret = ir->config.input.ops->open(ir);
        if (ret != RT_EOK)
        {
            goto fail_input_open;
        }
    }
    if (ir->config.output.ops)
    {
        ret = ir->config.output.ops->open(ir);
        if (ret != RT_EOK)
        {
            goto fail_output_open;
        }
    }
    if (ir->rx.frame_storage)
    {
        rt_ringbuffer_reset(&ir->rx.frame_queue);
    }
    ir->opened = RT_TRUE;
    if (ir->config.input.ops)
    {
        ret = infrared_device_receive_enable(ir, RT_TRUE);
        if (ret != RT_EOK)
        {
            goto fail_start;
        }
    }
    LOG_I("%s: opened", ir->config.name);
    return RT_EOK;

fail_start:
    ir->opened = RT_FALSE;
    if (ir->config.output.ops)
    {
        close_ret = ir->config.output.ops->close(ir);
        if (close_ret != RT_EOK)
        {
            LOG_E("%s: TX open rollback failed (%d)", ir->config.name, close_ret);
        }
    }
fail_output_open:
    if (ir->config.input.ops)
    {
        close_ret = ir->config.input.ops->close(ir);
        if (close_ret != RT_EOK)
        {
            LOG_E("%s: RX open rollback failed (%d)", ir->config.name, close_ret);
        }
    }
fail_input_open:
    /* Backends own their own failed-open rollback; do not close them a second time. */
    infrared_frame_release(ir);
    LOG_E("%s: device open failed (%d)", ir->config.name, ret);
    return ret;
}

/**
 * @brief Stop device capture/output and release resources previously acquired at open.
 * @param dev Address of infrared_device.parent.
 * @return RT_EOK or the first adapter close error; all directions are attempted once.
 * @note The caller serializes poll/write with close. Failed physical teardown is
 *       reported rather than retried by the framework.
 */
static rt_err_t infrared_rt_close(rt_device_t dev)
{
    struct infrared_device *ir = (struct infrared_device *)dev;
    rt_err_t ret = RT_EOK;
    rt_err_t close_ret; /* Report the first failure without skipping the other direction. */

    /* One best-effort close per backend, not a multi-stage recovery machine. */
    if (ir->config.output.ops)
    {
        close_ret = ir->config.output.ops->close(ir);
        if (close_ret != RT_EOK)
        {
            LOG_E("%s: TX close failed (%d)", ir->config.name, close_ret);
            ret = close_ret;
        }
    }
    if (ir->config.input.ops)
    {
        close_ret = ir->config.input.ops->close(ir);
        if (close_ret != RT_EOK)
        {
            LOG_E("%s: RX close failed (%d)", ir->config.name, close_ret);
            if (ret == RT_EOK)
            {
                ret = close_ret;
            }
        }
        /* A diagnostic STOP error can coexist with a successful physical
         * detach/close: release the frame pool only when the input claim is gone. */
        if (!ir->rx.claimed_input)
        {
            infrared_frame_release(ir);
        }
    }
    ir->opened = RT_FALSE;
    if (ret == RT_EOK)
    {
        LOG_I("%s: closed", ir->config.name);
    }
    return ret;
}

/**
 * @brief Read an array of completed frames from the infrared logical device.
 * @param dev Address of infrared_device.parent.
 * @param pos Unused position for record-oriented frame I/O.
 * @param buffer Caller-owned destination frame array.
 * @param size Array capacity in frames, not bytes.
 * @return Number of complete frames copied.
 */
static rt_size_t infrared_rt_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct infrared_device *ir = (struct infrared_device *)dev;
    (void)pos;
#if IR_CFG_CHECK_PARAMETERS
    if (!buffer && size)
    {
        return 0;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    if (!ir->opened || !ir->rx.frame_storage || !size)
    {
        return 0;
    }
    return infrared_frame_get(ir, (struct infrared_frame *)buffer, size);
}

/**
 * @brief Public frame-read API matching RT-Thread's frame-count semantics.
 * @param device Registered receive instance.
 * @param frames Destination complete-frame array.
 * @param count Array capacity in records.
 * @return Number of complete records read.
 */
rt_size_t infrared_device_read_frames(struct infrared_device *device,
                                      struct infrared_frame *frames, rt_size_t count)
{
#if IR_CFG_CHECK_PARAMETERS
    if (!device || !frames || !device->opened || !count)
    {
        return 0;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
    if (!device->rx.frame_storage || !device->opened || !count)
    {
        return 0;
    }
    return infrared_frame_get(device, frames, count);
}

/**
 * @brief Transmit a frame array from the infrared logical RT-Thread device.
 * @param dev Address of infrared_device.parent.
 * @param pos Unused stream position.
 * @param buffer Caller-owned array of infrared_frame records.
 * @param size Requested frame count, not byte count.
 * @return Number of complete frames transmitted.
 */
static rt_size_t infrared_rt_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    (void)pos;
    return infrared_device_write_frames((struct infrared_device *)dev,
                                        (const struct infrared_frame *)buffer, size);
}

/**
 * @brief Execute the infrared logical device's receive and status commands.
 * @param dev Address of infrared_device.parent.
 * @param cmd Infrared control command identifier.
 * @param args Optional command-specific data pointer.
 * @return RT_EOK or a negative RT-Thread error.
 */
static rt_err_t infrared_rt_control(rt_device_t dev, int cmd, void *args)
{
    struct infrared_device *ir = (struct infrared_device *)dev;
    switch (cmd)
    {
    case INFRARED_CTRL_RX_ENABLE:
#if IR_CFG_CHECK_PARAMETERS
        if (!args)
        {
            return -RT_EINVAL;
        }
#endif /* IR_CFG_CHECK_PARAMETERS */
        return infrared_device_receive_enable(ir, *(rt_bool_t *)args);
    case INFRARED_CTRL_RX_FLUSH:
        if (!ir->config.input.ops)
        {
            return -RT_ENOSYS;
        }
        if (!args || !ir->opened)
        {
            return -RT_EINVAL;
        }
        if (*(const rt_bool_t *)args == RT_FALSE)
        {
            rt_base_t level = rt_hw_interrupt_disable();
            ir->rx.flush_pending = RT_TRUE;
            rt_hw_interrupt_enable(level);
            infrared_device_notify_rx(ir);
            return RT_EOK;
        }
        else
        {
            rt_bool_t old_pending;
            rt_bool_t old_overflow;
            rt_base_t level = rt_hw_interrupt_disable();
            rt_err_t ret;
            old_pending = ir->rx.flush_pending;
            old_overflow = ir->rx.overflow;
            ir->rx.flush_pending = RT_FALSE;
            ir->rx.overflow = RT_FALSE;
            rt_hw_interrupt_enable(level);
            ret = infrared_flush_receive(ir, RT_TRUE);
            if (ret != RT_EOK)
            {
                level = rt_hw_interrupt_disable();
                ir->rx.flush_pending |= old_pending;
                ir->rx.overflow |= old_overflow;
                rt_hw_interrupt_enable(level);
            }
            return ret;
        }
    default:
        return -RT_ENOSYS;
    }
}

#ifdef RT_USING_DEVICE_OPS
/**
 * @brief RT-Thread device method table shared by all registered infrared instances.
 */
static const struct rt_device_ops infrared_rt_ops = {
    RT_NULL, infrared_rt_open, infrared_rt_close,
    infrared_rt_read, infrared_rt_write, infrared_rt_control
};
#endif /* RT_USING_DEVICE_OPS */

/**
 * @brief Register a logical infrared device using framework-owned storage.
 * @param config Static backend settings, index, name and protocol selection.
 * @return RT_EOK on success, or a negative registration error.
 */
rt_err_t infrared_device_register(const struct infrared_device_config *config)
{
    rt_err_t ret;
    struct infrared_device *device;
#if IR_CFG_CHECK_PARAMETERS
    if (!config || !config->name || !config->codec.ops ||
        config->index >= INFRARED_MAX_DEVICES ||
        (!config->input.ops && !config->output.ops))
    {
        LOG_E("registration: invalid device arguments");
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_PARAMETERS */
#if IR_CFG_CHECK_RESOURCE
    if (infrared_devices[config->index].registered)
    {
        LOG_E("registration: index %u already occupied", (unsigned)config->index);
        return -RT_EBUSY;
    }
#endif /* IR_CFG_CHECK_RESOURCE */
#if IR_CFG_CHECK_CONFIG
    if (config->input.ops &&
        (config->input.ops->direction != INFRARED_BACKEND_INPUT ||
         !config->input.ops->open || !config->input.ops->start ||
         !config->input.ops->stop || !config->input.ops->close ||
         !config->input.ops->io.rx.read || !config->input.ops->io.rx.flush ||
         !config->codec.ops->decode || !config->codec.ops->reset))
    {
        LOG_E("registration: invalid input/codec operations");
        return -RT_EINVAL;
    }
    if (config->output.ops &&
        (config->output.ops->direction != INFRARED_BACKEND_OUTPUT ||
         !config->output.ops->open || !config->output.ops->start ||
         !config->output.ops->stop || !config->output.ops->close ||
         !config->output.ops->io.tx.indicate || !config->codec.ops->encode))
    {
        LOG_E("registration: invalid output/codec operations");
        return -RT_EINVAL;
    }
    if ((config->input.ops && !config->input.settings) ||
        (config->output.ops && !config->output.settings))
    {
        LOG_E("registration: missing backend settings");
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_CONFIG */

    device = &infrared_devices[config->index];
    rt_memset(device, 0, sizeof(struct infrared_device));
    device->config = *config;
    device->registry_index = config->index;
    if (config->input.ops && !device->config.codec.context &&
        device->config.codec.ops->context_for_index)
    {
        device->config.codec.context = device->config.codec.ops->context_for_index(config->index);
    }
#if IR_CFG_CHECK_CONFIG
    if (config->input.ops && device->config.codec.context == RT_NULL)
    {
        LOG_E("registration: %s has no codec context", config->name);
        return -RT_EINVAL;
    }
#endif /* IR_CFG_CHECK_CONFIG */
    rt_completion_init(&device->tx.done);
    device->parent.type = RT_Device_Class_Miscellaneous;
#ifdef RT_USING_DEVICE_OPS
    device->parent.ops = &infrared_rt_ops;
#else
    device->parent.init = RT_NULL;
    device->parent.open = infrared_rt_open;
    device->parent.close = infrared_rt_close;
    device->parent.read = infrared_rt_read;
    device->parent.write = infrared_rt_write;
    device->parent.control = infrared_rt_control;
#endif /* RT_USING_DEVICE_OPS */
    ret = rt_device_register(&device->parent, config->name,
                             RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE);
    if (ret != RT_EOK)
    {
        LOG_E("registration: %s failed (%d)", config->name, ret);
        rt_memset(device, 0, sizeof(struct infrared_device));
        return ret;
    }
    device->registered = RT_TRUE;
    infrared_device_count++;
    LOG_I("registered %s at index %u", config->name, (unsigned)config->index);
    return RT_EOK;
}
