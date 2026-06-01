/**
 * @file    protocol.c
 * @brief   SCO communication protocol implementation.
 */

#include "protocol.h"
#include <string.h>

/* ── CRC16 Table ────────────────────────────────────────────────────────── */

static uint16_t crc16_table[256];
static bool crc_table_ready = false;

static void crc16_init_table(void)
{
    if (crc_table_ready) return;
    for (int i = 0; i < 256; i++) {
        uint16_t crc = (uint16_t)(i << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
        crc16_table[i] = crc;
    }
    crc_table_ready = true;
}

uint16_t proto_crc16(const uint8_t *data, size_t len)
{
    crc16_init_table();
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)(((crc >> 8) ^ data[i]) & 0xFF);
        crc = (uint16_t)((crc << 8) ^ crc16_table[idx]);
    }
    return crc;
}

/* ── Parser State Machine ───────────────────────────────────────────────── */

void proto_parser_init(ProtoParser *p)
{
    p->state    = PROTO_STATE_IDLE;
    p->cmd      = 0;
    p->data_len = 0;
    p->data_idx = 0;
    p->crc      = 0;
    p->crc_low  = 0;
}

bool proto_parser_feed(ProtoParser *p, uint8_t byte)
{
    switch (p->state) {
    case PROTO_STATE_IDLE:
        if (byte == PROTO_SYNC1) {
            p->state = PROTO_STATE_GOT_SYNC1;
        }
        break;

    case PROTO_STATE_GOT_SYNC1:
        if (byte == PROTO_SYNC2) {
            p->state = PROTO_STATE_GOT_SYNC2;
        } else if (byte != PROTO_SYNC1) {
            p->state = PROTO_STATE_IDLE;
        }
        break;

    case PROTO_STATE_GOT_SYNC2:
        p->cmd   = byte;
        p->state = PROTO_STATE_GOT_CMD;
        /* Start CRC accumulation over CMD */
        p->crc = proto_crc16(&byte, 1);
        break;

    case PROTO_STATE_GOT_CMD:
        p->data_len = (uint16_t)(byte << 8);
        p->state    = PROTO_STATE_GOT_LEN_H;
        /* Accumulate CRC over LEN_H */
        {
            uint16_t tmp = proto_crc16(&byte, 1);
            /* Combine CRCs: CRC(AB) = CRC(B with init=CRC(A)) */
            uint8_t idx = (uint8_t)(((p->crc >> 8) ^ byte) & 0xFF);
            p->crc = (uint16_t)((p->crc << 8) ^ crc16_table[idx]);
            (void)tmp;
        }
        break;

    case PROTO_STATE_GOT_LEN_H:
        p->data_len |= byte;
        p->state     = PROTO_STATE_GOT_LEN_L;
        {
            uint8_t idx = (uint8_t)(((p->crc >> 8) ^ byte) & 0xFF);
            p->crc = (uint16_t)((p->crc << 8) ^ crc16_table[idx]);
        }
        if (p->data_len > PROTO_MAX_DATA_LEN) {
            p->state = PROTO_STATE_IDLE;
            return false;
        }
        if (p->data_len == 0) {
            p->state    = PROTO_STATE_CRC1;
            p->data_idx = 0;
        } else {
            p->state    = PROTO_STATE_DATA;
            p->data_idx = 0;
        }
        break;

    case PROTO_STATE_DATA:
        p->data_buf[p->data_idx++] = byte;
        {
            uint8_t idx = (uint8_t)(((p->crc >> 8) ^ byte) & 0xFF);
            p->crc = (uint16_t)((p->crc << 8) ^ crc16_table[idx]);
        }
        if (p->data_idx >= p->data_len) {
            p->state = PROTO_STATE_CRC1;
        }
        break;

    case PROTO_STATE_CRC1:
        p->crc_low = byte;
        p->state   = PROTO_STATE_CRC2;
        break;

    case PROTO_STATE_CRC2:
        {
            uint16_t rx_crc = (uint16_t)((byte << 8) | p->crc_low);
            if (rx_crc == p->crc) {
                p->state = PROTO_STATE_IDLE;
                return true;  /* valid packet */
            }
            p->state = PROTO_STATE_IDLE;
        }
        break;
    }
    return false;
}

/* ── Packet Building ────────────────────────────────────────────────────── */

size_t proto_build_response(uint8_t cmd, const uint8_t *data, uint16_t len,
                            uint8_t *buf, size_t buf_size)
{
    size_t total = 2 + 3 + len + 2;  /* sync2 + header + data + crc2 */
    if (total > buf_size) return 0;

    size_t pos = 0;
    buf[pos++] = PROTO_SYNC1;
    buf[pos++] = PROTO_SYNC2;
    buf[pos++] = cmd;
    buf[pos++] = (uint8_t)((len >> 8) & 0xFF);
    buf[pos++] = (uint8_t)(len & 0xFF);

    if (len > 0) {
        memcpy(&buf[pos], data, len);
        pos += len;
    }

    /* CRC over cmd + len_h + len_l + data */
    uint16_t crc = proto_crc16(&buf[2], (uint16_t)(3 + len));
    buf[pos++] = (uint8_t)(crc & 0xFF);
    buf[pos++] = (uint8_t)((crc >> 8) & 0xFF);

    return pos;
}

size_t proto_build_status(bool sg_on, bool scope_on, uint8_t waveform,
                          uint32_t freq_hz, uint16_t amp_mv,
                          uint8_t trig_mode, uint16_t trig_level_mv,
                          uint32_t sample_rate, uint8_t *buf, size_t buf_size)
{
    /* Format: sg_on(B) scope_on(B) waveform(B) freq(I) amp(H)
                trig_mode(B) trig_level(H) sample_rate(I) = 16 bytes */
    uint8_t data[16];
    data[0] = sg_on ? 1 : 0;
    data[1] = scope_on ? 1 : 0;
    data[2] = waveform;
    data[3] = (uint8_t)(freq_hz & 0xFF);
    data[4] = (uint8_t)((freq_hz >> 8) & 0xFF);
    data[5] = (uint8_t)((freq_hz >> 16) & 0xFF);
    data[6] = (uint8_t)((freq_hz >> 24) & 0xFF);
    data[7] = (uint8_t)(amp_mv & 0xFF);
    data[8] = (uint8_t)((amp_mv >> 8) & 0xFF);
    data[9] = trig_mode;
    data[10] = (uint8_t)(trig_level_mv & 0xFF);
    data[11] = (uint8_t)((trig_level_mv >> 8) & 0xFF);
    data[12] = (uint8_t)(sample_rate & 0xFF);
    data[13] = (uint8_t)((sample_rate >> 8) & 0xFF);
    data[14] = (uint8_t)((sample_rate >> 16) & 0xFF);
    data[15] = (uint8_t)((sample_rate >> 24) & 0xFF);
    return proto_build_response(RSP_STATUS, data, 16, buf, buf_size);
}

size_t proto_build_scope_data(uint16_t seq, const uint16_t *samples,
                              uint16_t count, uint8_t *buf, size_t buf_size)
{
    size_t data_size = 2 + 2 + (size_t)count * 2;  /* seq + count + samples */
    if (data_size > PROTO_MAX_DATA_LEN) return 0;

    uint8_t data[PROTO_MAX_DATA_LEN];
    data[0] = (uint8_t)(seq & 0xFF);
    data[1] = (uint8_t)((seq >> 8) & 0xFF);
    data[2] = (uint8_t)(count & 0xFF);
    data[3] = (uint8_t)((count >> 8) & 0xFF);

    for (uint16_t i = 0; i < count; i++) {
        data[4 + i * 2]     = (uint8_t)(samples[i] & 0xFF);
        data[4 + i * 2 + 1] = (uint8_t)((samples[i] >> 8) & 0xFF);
    }

    return proto_build_response(RSP_SCOPE_DATA, data, (uint16_t)data_size,
                                buf, buf_size);
}

size_t proto_build_error(uint8_t code, uint8_t *buf, size_t buf_size)
{
    return proto_build_response(RSP_ERROR, &code, 1, buf, buf_size);
}
