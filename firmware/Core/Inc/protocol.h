/**
 * @file    protocol.h
 * @brief   SCO communication protocol definitions (shared with PC side).
 *
 * Packet format:
 *   [SYNC1 0xAA] [SYNC2 0x55] [CMD 1B] [LEN_H 1B] [LEN_L 1B] [DATA N] [CRC16 2B LE]
 *
 * CRC16-CCITT over CMD + LEN_H + LEN_L + DATA.
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */

#define PROTO_SYNC1  0xAA
#define PROTO_SYNC2  0x55

/* PC → MCU Commands */
#define CMD_SET_WAVEFORM     0x01
#define CMD_SET_FREQ         0x02
#define CMD_SET_AMPLITUDE    0x03
#define CMD_SET_SAMPLE_RATE  0x04
#define CMD_SET_TRIGGER      0x05
#define CMD_START_SG         0x06
#define CMD_STOP_SG          0x07
#define CMD_START_SCOPE      0x08
#define CMD_STOP_SCOPE       0x09
#define CMD_GET_STATUS       0x0A

/* MCU → PC Responses */
#define RSP_STATUS      0x80
#define RSP_SCOPE_DATA  0x81
#define RSP_ERROR       0x82

/* Waveform types */
#define WAVE_SINE       0
#define WAVE_SQUARE     1
#define WAVE_TRIANGLE   2
#define WAVE_SAWTOOTH   3

/* Trigger modes */
#define TRIG_AUTO       0
#define TRIG_RISING     1
#define TRIG_FALLING    2

/* Error codes */
#define ERR_NONE        0x00
#define ERR_BAD_CMD     0x01
#define ERR_BAD_PARAM   0x02
#define ERR_BUSY        0x03
#define ERR_CRC         0x04
#define ERR_OVERRUN     0x05

/* Packet limits */
#define PROTO_MAX_DATA_LEN  2048
#define PROTO_BUF_SIZE      (PROTO_MAX_DATA_LEN + 7)  /* sync2 + cmd + len2 + data + crc2 */

/* ── Types ─────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  cmd;
    uint16_t data_len;
    uint8_t  data[PROTO_MAX_DATA_LEN];
} ProtoPacket;

typedef enum {
    PROTO_STATE_IDLE,
    PROTO_STATE_GOT_SYNC1,
    PROTO_STATE_GOT_SYNC2,
    PROTO_STATE_GOT_CMD,
    PROTO_STATE_GOT_LEN_H,
    PROTO_STATE_GOT_LEN_L,
    PROTO_STATE_DATA,
    PROTO_STATE_CRC1,
    PROTO_STATE_CRC2,
} ProtoState;

typedef struct {
    ProtoState state;
    uint8_t    cmd;
    uint16_t   data_len;
    uint16_t   data_idx;
    uint8_t    data_buf[PROTO_MAX_DATA_LEN];
    uint16_t   crc;
    uint8_t    crc_low;
} ProtoParser;

/* ── API ───────────────────────────────────────────────────────────────── */

/**
 * @brief  Compute CRC16-CCITT over a buffer.
 */
uint16_t proto_crc16(const uint8_t *data, size_t len);

/**
 * @brief  Initialize the protocol parser state machine.
 */
void proto_parser_init(ProtoParser *p);

/**
 * @brief  Feed one byte into the parser.
 * @return true if a complete packet was parsed (available in p->data_buf).
 *         Caller should then re-init the parser.
 */
bool proto_parser_feed(ProtoParser *p, uint8_t byte);

/**
 * @brief  Build a response packet into buf. Returns total packet length.
 */
size_t proto_build_response(uint8_t cmd, const uint8_t *data, uint16_t len,
                            uint8_t *buf, size_t buf_size);

/**
 * @brief  Convenience: build STATUS response.
 */
size_t proto_build_status(bool sg_on, bool scope_on, uint8_t waveform,
                          uint32_t freq_hz, uint16_t amp_mv,
                          uint8_t trig_mode, uint16_t trig_level_mv,
                          uint32_t sample_rate, uint8_t *buf, size_t buf_size);

/**
 * @brief  Convenience: build SCOPE_DATA response.
 */
size_t proto_build_scope_data(uint16_t seq, const uint16_t *samples,
                              uint16_t count, uint8_t *buf, size_t buf_size);

/**
 * @brief  Convenience: build ERROR response.
 */
size_t proto_build_error(uint8_t code, uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H */
