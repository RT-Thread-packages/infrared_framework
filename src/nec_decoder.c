/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-03-25     balanceTWK   the first version
 */

#include <infrared.h>
#include "ipc/ringbuffer.h"

#define DBG_SECTION_NAME     "nec.decoder"
#define DBG_LEVEL     DBG_INFO
#include <rtdbg.h>

#ifdef INFRARED_NEC_DECODER

#define NEC_BUFF_SIZE  32

static struct decoder_class nec_decoder;
static struct rt_ringbuffer *ringbuff;

struct nec_data_struct nec_data_buf[NEC_BUFF_SIZE];

static rt_err_t nec_decoder_init(void)
{
    ringbuff = rt_ringbuffer_create(sizeof(nec_data_buf));
    if(ringbuff)
    {
        nec_decoder.user_data = ringbuff;
        return RT_EOK;
    }
    else
    {
        return -RT_ERROR;
    }
}

static rt_err_t nec_decoder_deinit(void)
{
    rt_ringbuffer_destroy(ringbuff);
    return RT_EOK;
}

static rt_err_t nec_decoder_read(struct infrared_decoder_data* nec_data)
{
    if( rt_ringbuffer_get(ringbuff, (rt_uint8_t*)&(nec_data->data.nec), sizeof(struct nec_data_struct)) == sizeof(struct nec_data_struct) ) //;//== struct nec_data_struct
    {
        LOG_D("NEC addr:0x%01X key:0x%01X repeat:%d",nec_data->data.nec.addr,nec_data->data.nec.key,nec_data->data.nec.repeat);
        return RT_EOK;
    }
    else
    {
        return -RT_ERROR;
    }
}

static rt_err_t nec_decoder_control(int cmd, void *arg)
{
    return RT_EOK;
}

static rt_err_t nec_decoder_decode(rt_size_t size)
{
    static rt_uint8_t nec_state = 0;
    struct ir_raw_data raw_data[200];
    
    static struct ir_raw_data state_code[2];
    
    static struct nec_data_struct nec_data;

    static rt_uint32_t command;
    rt_uint8_t t1, t2;

    LOG_D("size:%d",size);
    if(nec_state == 0x01)
    {
        if(size==65)
        {
            for(rt_uint8_t i=0; i<65; i++)
            {
                decoder_read_data(&raw_data[i]);
                if(raw_data[i].level == IDLE_SIGNAL)
                {
                    LOG_D("IDLE_SIGNAL,LINE:%d",__LINE__);
                    if((raw_data[i].us>1600)&&(raw_data[i].us<1800))
                    {
                        LOG_D(" 1 LINE:%d",__LINE__);
                        command <<= 1;
                        command |= 1;
                    }
                    else if((raw_data[i].us>450)&&(raw_data[i].us<650))
                    {
                        LOG_D(" 0 LINE:%d",__LINE__);
                        command <<= 1;
                        command |= 0;
                    }
                }
                else if((i == 64)&&((raw_data[i].us>450)&&(raw_data[i].us<650)))
                {
                    t1 = command >> 8;
                    t2 = command;
                    LOG_D("1 t1:0x%01X t2:0x%01X",t1,t2);
                    if (t1 == (rt_uint8_t)~t2)
                    {
                        nec_data.key = t1;

                        t1 = command >> 24;
                        t2 = command >>16;
                        LOG_D("2 t1:0x%01X t2:0x%01X",t1,t2);
                        if(t1 == (rt_uint8_t)~t2)
                        {
                            nec_data.addr = t1;
                            nec_data.repeat = 0;
                            rt_ringbuffer_put(ringbuff, (rt_uint8_t*)&nec_data, sizeof(struct nec_data_struct));
                            LOG_D("OK");
                            nec_state = 0x00;
                        }
                        else
                        {
                            nec_state = 0x00;
                            nec_data.addr = 0;
                            nec_data.key = 0;
                            nec_data.repeat = 0;
                        }
                    }
                    else
                    {
                        nec_state = 0x00;
                        nec_data.addr = 0;
                        nec_data.key = 0;
                        nec_data.repeat = 0;
                    }
                }
            }
        }
    }
    else if(nec_state == 0x04)
    {
        decoder_read_data(&state_code[1]);
        if((state_code[1].level == IDLE_SIGNAL) && ((state_code[1].us > 4000)&&(state_code[1].us < 5000)))//�ж���������
        {
            nec_state = 0x01;
            LOG_D("guidance");
        }
        else if((state_code[1].level == IDLE_SIGNAL) && ((state_code[1].us > 2150)&&(state_code[1].us < 2350)))//�ж����ظ���
        {
            nec_data.repeat++;
            nec_state = 0x00;
            rt_ringbuffer_put(ringbuff, (rt_uint8_t*)&nec_data, sizeof(struct nec_data_struct));
            LOG_D("repeat");
        }
        else
        {
            nec_data.repeat = 0;
            nec_state = 0x00;
            LOG_D("no guidance");
            state_code[0].level = NO_SIGNAL;
            state_code[1].level = NO_SIGNAL;
            return -RT_ERROR;
        }
    }
    else
    {
        decoder_read_data(&state_code[0]);
        if((state_code[0].level == CARRIER_WAVE) && ((state_code[0].us > 8500)&&(state_code[0].us < 9500)))//�ж��Ƿ���(������ | �ظ���)
        {
            nec_state = 0x04;
        }
        else
        {
            nec_state = 0x00;
            LOG_D("no 9000us:%d",state_code[0].us);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

static rt_err_t nec_decoder_write(struct infrared_decoder_data* data)
{
    struct ir_raw_data raw_data[100];
    rt_uint8_t addr,key;
    rt_uint32_t data_buff;

    addr = data->data.nec.addr;
    key = data->data.nec.key;

    data_buff = ((addr & 0xFF) << 24) + ((~addr & 0xFF) << 16) + ((key & 0xff) << 8) + (~key & 0xFF);

    /* guidance code */
    raw_data[0].level = CARRIER_WAVE;
    raw_data[0].us = 9000;
    raw_data[1].level = IDLE_SIGNAL;
    raw_data[1].us = 4500;

    for(rt_uint8_t index = 0; index < 64; index+=2)
    {
        if(((data_buff << (index/2)) & 0x80000000))  /* Logic 1 */
        {
            raw_data[2+index].level = CARRIER_WAVE;
            raw_data[2+index].us = 560;
            raw_data[2+index+1].level = IDLE_SIGNAL;
            raw_data[2+index+1].us = 1690;
        }
        else                                         /* Logic 0 */
        {
            raw_data[2+index].level = CARRIER_WAVE;
            raw_data[2+index].us = 560;
            raw_data[2+index+1].level = IDLE_SIGNAL;
            raw_data[2+index+1].us = 560;
        }
    }

    /* epilog code */
    raw_data[66].level = CARRIER_WAVE;
    raw_data[66].us = 560;
    raw_data[67].level = IDLE_SIGNAL;
    raw_data[67].us = 43580;

    if(data->data.nec.repeat>8)
    {
        data->data.nec.repeat = 8;
    }
    /* repetition code */
    for(rt_uint32_t i=0; i<(4 * data->data.nec.repeat); i+=4)
    {
        raw_data[68+i].level = CARRIER_WAVE;
        raw_data[68+i].us = 9000;
        raw_data[68+i+1].level = IDLE_SIGNAL;
        raw_data[68+i+1].us = 2250;
        raw_data[68+i+2].level = CARRIER_WAVE;
        raw_data[68+i+2].us = 560;
        raw_data[68+i+3].level = IDLE_SIGNAL;
        raw_data[68+i+3].us = 43580;
    }

    LOG_D("%d size:%d + %d",sizeof(struct ir_raw_data),68 ,(data->data.nec.repeat) * 4);
    decoder_write_data(raw_data,68 + (data->data.nec.repeat) * 4);

    rt_thread_mdelay(200);

    return RT_EOK;
}

static struct decoder_ops ops;

int nec_decoder_register()
{
    nec_decoder.name = "nec";

    ops.control = nec_decoder_control;
    ops.decode = nec_decoder_decode;
    ops.init = nec_decoder_init;
    ops.deinit = nec_decoder_deinit;
    ops.read = nec_decoder_read;
    ops.write = nec_decoder_write;

    nec_decoder.ops = &ops;

    ir_decoder_register(&nec_decoder);
    return 0;
}
INIT_APP_EXPORT(nec_decoder_register);

#endif /* INFRARED_NEC_DECODER */
