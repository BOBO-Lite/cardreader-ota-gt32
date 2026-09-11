/**
@file drv_ymodem.c
@brief 无状态 YMODEM 报文、CRC16、包号与 Block0 判定实现
@note 语义参考 /workspace/ref-v031/code/USER/DRIVERS/src/drv_ymodem.c；按 C 规范重写。
      本模块不发送、不重装 DMA、不推进 Boot 相位。
*/

#include "drv_ymodem.h"

#include <string.h>

#include "ota_layout.h"
#include "ota_meta_codec.h"

DrvYmFrameKind_t DrvYmodem_Classify(uint8_t first_byte)
{
    switch (first_byte) {
    case DRV_YM_SOH:
        return DRV_YM_FRAME_SOH;
    case DRV_YM_STX:
        return DRV_YM_FRAME_STX;
    case DRV_YM_EOT:
        return DRV_YM_FRAME_EOT;
    case DRV_YM_CAN:
        return DRV_YM_FRAME_CAN;
    default:
        return DRV_YM_FRAME_UNKNOWN;
    }
}

size_t DrvYmodem_ExpectedLen(DrvYmFrameKind_t kind)
{
    switch (kind) {
    case DRV_YM_FRAME_SOH:
        return DRV_YM_SOH_FRAME_LEN;
    case DRV_YM_FRAME_STX:
        return DRV_YM_STX_FRAME_LEN;
    case DRV_YM_FRAME_EOT:
    case DRV_YM_FRAME_CAN:
        return 1u;
    default:
        return 0u;
    }
}

uint16_t DrvYmodem_Crc16Ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0u;
    size_t i;
    int b;

    if (data == NULL && len > 0u) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0; b < 8; b++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

int DrvYmodem_CheckSeq(uint8_t seq, uint8_t seq_comp)
{
    return (seq_comp == (uint8_t)(0xFFu - seq)) ? 1 : 0;
}

int DrvYmodem_CheckFrameCrc(const uint8_t *frame, size_t frame_len)
{
    DrvYmFrame_t view;
    return (DrvYmodem_ParseRound(frame, (uint16_t)frame_len, &view)
            == DRV_YM_ITEM_FRAME) ? 1 : 0;
}

/**
@brief 本轮是否全为 CAN 且长度≥2（取消序列）
*/
static int IsCancelSequence(const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (length < 2u) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        if (data[i] != DRV_YM_CAN) {
            return 0;
        }
    }
    return 1;
}

DrvYmItem_t DrvYmodem_ParseRound(const uint8_t *data,
                                 uint16_t length,
                                 DrvYmFrame_t *frame)
{
    uint16_t payload_length;
    uint16_t received_crc;

    if (length == 0u) {
        return DRV_YM_ITEM_EMPTY;
    }
    if (data == NULL || frame == NULL) {
        return DRV_YM_ITEM_INVALID;
    }

    if (length == 1u) {
        if (data[0] == DRV_YM_CAN) {
            return DRV_YM_ITEM_CAN;
        }
        if (data[0] == DRV_YM_EOT) {
            return DRV_YM_ITEM_EOT;
        }
        return DRV_YM_ITEM_INVALID;
    }

    if (IsCancelSequence(data, length) != 0) {
        return DRV_YM_ITEM_CANCEL;
    }

    if (length == DRV_YM_SOH_FRAME_LEN && data[0] == DRV_YM_SOH) {
        payload_length = (uint16_t)DRV_YM_SOH_PAYLOAD_LEN;
    } else if (length == DRV_YM_STX_FRAME_LEN && data[0] == DRV_YM_STX) {
        payload_length = (uint16_t)DRV_YM_STX_PAYLOAD_LEN;
    } else {
        return DRV_YM_ITEM_INVALID;
    }

    if (!DrvYmodem_CheckSeq(data[1], data[2])) {
        return DRV_YM_ITEM_INVALID;
    }

    received_crc = (uint16_t)(((uint16_t)data[payload_length + 3u] << 8)
                              | (uint16_t)data[payload_length + 4u]);
    if (DrvYmodem_Crc16Ccitt(&data[3], payload_length) != received_crc) {
        return DRV_YM_ITEM_INVALID;
    }

    frame->payload = &data[3];
    frame->payload_length = payload_length;
    frame->payload_crc16 = received_crc;
    frame->packet_number = data[1];
    return DRV_YM_ITEM_FRAME;
}

int DrvYmodem_ParseHeader(const DrvYmFrame_t *frame,
                          DrvYmHeaderInfo_t *header)
{
    uint16_t name_length;
    uint16_t index;
    uint32_t file_size;
    int digit_seen;
    char name_buf[32];
    OtaMetaStatus_t mst;

    if (frame == NULL || header == NULL || frame->payload == NULL
        || frame->payload_length == 0u || frame->packet_number != 0u) {
        return 0;
    }

    for (name_length = 0u; name_length < frame->payload_length; name_length++) {
        uint8_t ch = frame->payload[name_length];
        if (ch == 0u) {
            break;
        }
        if (ch < 0x20u || ch > 0x7Eu
            || name_length >= (ota_meta_len_filename - 1u)) {
            return 0;
        }
    }
    if (name_length == 0u || name_length >= frame->payload_length) {
        return 0;
    }
    if (name_length >= sizeof(name_buf)) {
        return 0;
    }

    (void)memcpy(name_buf, frame->payload, name_length);
    name_buf[name_length] = '\0';
    mst = OtaMeta_ValidateFilename(name_buf);
    if (mst != OTA_META_OK) {
        return 0;
    }

    index = (uint16_t)(name_length + 1u);
    file_size = 0u;
    digit_seen = 0;
    while (index < frame->payload_length
           && frame->payload[index] >= (uint8_t)'0'
           && frame->payload[index] <= (uint8_t)'9') {
        uint8_t digit = (uint8_t)(frame->payload[index] - (uint8_t)'0');
        if (file_size > (0xFFFFFFFFu - (uint32_t)digit) / 10u) {
            return 0;
        }
        file_size = file_size * 10u + (uint32_t)digit;
        digit_seen = 1;
        index++;
    }
    if (digit_seen == 0) {
        return 0;
    }
    if (index < frame->payload_length
        && frame->payload[index] != 0u
        && frame->payload[index] != (uint8_t)' ') {
        return 0;
    }
    if (file_size < ota_app_image_min || file_size > ota_app_image_max) {
        return 0;
    }

    header->filename = frame->payload;
    header->filename_length = (uint8_t)name_length;
    header->file_size = file_size;
    return 1;
}

int DrvYmodem_IsEmptyHeader(const DrvYmFrame_t *frame)
{
    if (frame == NULL || frame->payload == NULL || frame->payload_length == 0u
        || frame->packet_number != 0u) {
        return 0;
    }
    return (frame->payload[0] == 0u) ? 1 : 0;
}

DrvYmPacketMatch_t DrvYmodem_ClassifyDataPacket(uint8_t packet_number,
                                                uint8_t expected_packet)
{
    if (packet_number == expected_packet) {
        return DRV_YM_PKT_EXPECTED;
    }
    if (packet_number == (uint8_t)(expected_packet - 1u)) {
        return DRV_YM_PKT_DUPLICATE;
    }
    return DRV_YM_PKT_UNEXPECTED;
}
