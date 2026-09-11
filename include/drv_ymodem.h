/**
@file drv_ymodem.h
@brief 无状态 YMODEM 报文判定（ParseRound/Header/包号分类；不发送、不重装 DMA、不推进相位）
@note 语义参考 /workspace/ref-v031/code/USER/DRIVERS/；按 C 规范重写，非原样粘贴。
*/

#ifndef CARDREADER_DRV_YMODEM_H
#define CARDREADER_DRV_YMODEM_H

#include <stddef.h>
#include <stdint.h>

#define DRV_YM_SOH (0x01u)
#define DRV_YM_STX (0x02u)
#define DRV_YM_EOT (0x04u)
#define DRV_YM_ACK (0x06u)
#define DRV_YM_NAK (0x15u)
#define DRV_YM_CAN (0x18u)
#define DRV_YM_C   (0x43u)

/** SOH 完整帧长：头+序+反码+128 数据+CRC16 */
#define DRV_YM_SOH_FRAME_LEN (133u)
/** STX 完整帧长：头+序+反码+1024 数据+CRC16 */
#define DRV_YM_STX_FRAME_LEN (1029u)
#define DRV_YM_SOH_PAYLOAD_LEN (128u)
#define DRV_YM_STX_PAYLOAD_LEN (1024u)

/**
@brief 帧首字节粗分类（辅助）
*/
typedef enum {
    DRV_YM_FRAME_UNKNOWN = 0,
    DRV_YM_FRAME_SOH,
    DRV_YM_FRAME_STX,
    DRV_YM_FRAME_EOT,
    DRV_YM_FRAME_CAN
} DrvYmFrameKind_t;

/**
@brief 一轮冻结字节的协议分类结果
*/
typedef enum {
    DRV_YM_ITEM_EMPTY = 0,
    DRV_YM_ITEM_CAN,
    DRV_YM_ITEM_CANCEL,
    DRV_YM_ITEM_EOT,
    DRV_YM_ITEM_FRAME,
    DRV_YM_ITEM_INVALID
} DrvYmItem_t;

/**
@brief 已通过长度/序反码/CRC16 的帧视图（payload 指向调用方缓冲）
*/
typedef struct {
    const uint8_t *payload;
    uint16_t payload_length;
    uint16_t payload_crc16;
    uint8_t packet_number;
} DrvYmFrame_t;

/**
@brief 数据包相对期望包号的关系
*/
typedef enum {
    DRV_YM_PKT_EXPECTED = 0,
    DRV_YM_PKT_DUPLICATE,
    DRV_YM_PKT_UNEXPECTED
} DrvYmPacketMatch_t;

/**
@brief 已验证非空 Block0 文件头视图（filename 指向帧内，Release 后失效）
*/
typedef struct {
    const uint8_t *filename;
    uint8_t filename_length;
    uint32_t file_size;
} DrvYmHeaderInfo_t;

/**
@brief 按首字节分类帧类型
@param first_byte 首字节
@return 帧种类
*/
DrvYmFrameKind_t DrvYmodem_Classify(uint8_t first_byte);

/**
@brief 期望完整帧长度（SOH=133，STX=1029，EOT/CAN=1，未知=0）
@param kind 帧种类
@return 字节数
*/
size_t DrvYmodem_ExpectedLen(DrvYmFrameKind_t kind);

/**
@brief CRC16-CCITT（poly 0x1021，初值 0，无终异或）
@param data 输入
@param len 长度
@return CRC16
*/
uint16_t DrvYmodem_Crc16Ccitt(const uint8_t *data, size_t len);

/**
@brief 包序号与反码校验
@param seq 包号
@param seq_comp 反码
@return 非 0 表示通过
*/
int DrvYmodem_CheckSeq(uint8_t seq, uint8_t seq_comp);

/**
@brief 对完整 SOH/STX 帧做长度/序/CRC 校验（辅助）
@param frame 完整帧
@param frame_len 实收长度
@return 非 0 表示通过
*/
int DrvYmodem_CheckFrameCrc(const uint8_t *frame, size_t frame_len);

/**
@brief 对一轮冻结 DMA 字节作无副作用 YMODEM 判定
@param data 接收字节；仅 length 非零时不可为 NULL
@param length 本轮长度（不与下一轮拼接）
@param frame 仅 ITEM_FRAME 时写入；不可为 NULL
@return 空轮/单 CAN/双 CAN 取消/EOT/有效帧/无效
@note 不访问 DMA、Flash、时间或发送；不保存跨轮状态
*/
DrvYmItem_t DrvYmodem_ParseRound(const uint8_t *data,
                                 uint16_t length,
                                 DrvYmFrame_t *frame);

/**
@brief 校验有效帧是否为合法非空 Block0 并提取文件名与大小
@param frame 已由 ParseRound 返回的有效帧
@param header 输出；不可为 NULL
@return 非 0 表示 cardreader-v* 且 size∈[8,112640]
*/
int DrvYmodem_ParseHeader(const DrvYmFrame_t *frame,
                          DrvYmHeaderInfo_t *header);

/**
@brief 判断有效帧是否为结束空 Block0（包号 0 且负载首字节 NUL）
@param frame 有效帧
@return 非 0 表示空头
*/
int DrvYmodem_IsEmptyHeader(const DrvYmFrame_t *frame);

/**
@brief 将数据包号与 Boot 期望包号比较
@param packet_number 当前帧包号
@param expected_packet 期望包号
@return EXPECTED / DUPLICATE（期望-1）/ UNEXPECTED
*/
DrvYmPacketMatch_t DrvYmodem_ClassifyDataPacket(uint8_t packet_number,
                                                uint8_t expected_packet);

#endif /* CARDREADER_DRV_YMODEM_H */
