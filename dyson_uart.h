#pragma once

#include "assert.h"
#include "math.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"

const uint8_t START_STOP_BYTE = 0x12;
const uint8_t STUFF_BYTE_1 = 0xDB;
const uint8_t STUFF_BYTE_2 = 0xDE;
const uint8_t STUFF_BYTE_3 = 0xDD;

typedef enum {
    UINT8 = 98,
    UINT16 = 100,
    UINT32 = 102,
    DOUBLE = 107,
} DataType_t;

typedef struct {
    uint32_t field_id;
    uint8_t param_count;
    uint8_t success;
    DataType_t data_type;
    union {
        uint8_t u8s[256];
        uint16_t u16s[128];
        uint32_t u32s[64];
        double d64s[32];
    } value;
} DysonParseResult_t;

class DysonUartParser {
  private:
    bool packetComplete;

  public:
    uint8_t packetLen;
    uint8_t buffer[255];

    DysonUartParser() {
        packetComplete = false;
        packetLen = 0;
    }

    // Sets out->success to 1 iff *out is filled with the next parsed packet.
    void putch(uint8_t in, DysonParseResult_t *out) {
        // If there's a finished packet, it was already processed last time
        if (packetComplete) {
            packetComplete = false;
            packetLen = 0;
        }

        // TODO: don't overflow the buffer when appending...

        if (packetLen == 0 && in == START_STOP_BYTE) { // Start of a new packet.
            buffer[packetLen++] = in;
            return;
        } else if (packetLen == 1 && buffer[0] == START_STOP_BYTE &&
                   in == START_STOP_BYTE) {
            // Previous start byte was actually a stop byte, because we started
            // receiving data in the middle of a packet; _this_ one is the real
            // start of the packet.
            return;
        } else if (packetLen > 0) {
            buffer[packetLen++] = in;
            if (in == START_STOP_BYTE) {
                packetComplete = true;
                unstuff();
                parse(out);
                return;
            } else {
                return;
            }
        } else {
            return;
        }
    }

  private:
    void unstuff() {
        assert(packetLen > 2);
        assert(buffer[0] == START_STOP_BYTE);
        assert(buffer[packetLen - 1] == START_STOP_BYTE);

        // Strip leading/trailing 0x12 bytes:
        uint8_t *src = &buffer[1], *dst = &buffer[0];

        for (; src < &buffer[packetLen - 1];) {
            if (*src == STUFF_BYTE_1) {
                if (*(src + 1) == STUFF_BYTE_2)
                    *dst = START_STOP_BYTE;
                else if (*(src + 1) == STUFF_BYTE_3)
                    *dst = STUFF_BYTE_1;
                src++;
            } else {
                *dst = *src;
            }
            dst++;
            src++;
        }

        *(dst++) = START_STOP_BYTE;

        packetLen = dst - buffer;
    }

    void parse(DysonParseResult_t *result) {
        result->success = 0;

        if (packetLen < 21)
            return;
        uint16_t statedPacketLen = *(uint16_t *)buffer;
        if (statedPacketLen != packetLen - 4)
            return;

        if (buffer[9] != 0x31)
            return;

        result->field_id = *(uint32_t *)&buffer[10];
        result->data_type = (DataType_t) * (uint8_t *)&buffer[14];
        result->param_count = *(uint8_t *)&buffer[15];

        const int data_size = result->data_type == DataType_t::UINT8    ? 1
                              : result->data_type == DataType_t::UINT16 ? 2
                              : result->data_type == DataType_t::UINT32 ? 4
                              : result->data_type == DataType_t::DOUBLE ? 8
                                                                        : 0;
        if (!data_size)
            return;

        if (packetLen < 16 + data_size * result->param_count)
            return;
        memcpy(result->value.u8s, &buffer[16], data_size * result->param_count);
        result->success = 1;
    }
};
