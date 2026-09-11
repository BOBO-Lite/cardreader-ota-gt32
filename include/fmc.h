/**
@file fmc.h
@brief Meta 页 + App 区 FMC 抽象（Meta 只擦第一页；主机可 Mock）
@note 语义对照 R02.1 §3.4.5 / §3.5；不粘贴 GD32 头文件。实机 FMC 未接线（非 E3）。
*/

#ifndef CARDREADER_FMC_H
#define CARDREADER_FMC_H

#include <stddef.h>
#include <stdint.h>

/**
@brief FMC 状态：0 成功，负值失败（API-004）
*/
typedef enum {
    FMC_OK = 0,
    FMC_ERR_NULL = -1,
    FMC_ERR_PARAM = -2,
    FMC_ERR_RANGE = -3,
    FMC_ERR_HARDWARE = -4
} FmcStatus_t;

/**
@brief 擦除 Meta 第一页（ota_flash_meta_page_base..page_end，1 KiB）
@note 永不擦第二页预留区。
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_EraseMetaPage(void);

/**
@brief 向 Meta 页相对偏移编程字节流并依赖调用方回读校验
@param offset 相对 ota_flash_meta_page_base 的字节偏移
@param data 源数据
@param len 字节数
@return FMC_OK 或负错误码
@note NOR：仅允许 1→0；脏字非目标值且非全 F 时失败（禁止补写）
*/
FmcStatus_t Fmc_ProgramMeta(uint32_t offset, const uint8_t *data, size_t len);

/**
@brief 从 Meta 页相对偏移读出
@param offset 相对页基址偏移
@param out 输出缓冲
@param len 字节数
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_ReadMeta(uint32_t offset, uint8_t *out, size_t len);

/**
@brief 比较 Meta 页相对偏移与期望缓冲
@param offset 相对页基址偏移
@param data 期望字节
@param len 字节数
@return FMC_OK 全匹配；否则 FMC_ERR_HARDWARE / 参数错误
*/
FmcStatus_t Fmc_CompareMeta(uint32_t offset, const uint8_t *data, size_t len);

/**
@brief 读 Meta 页内一个小端 uint32 字
@param offset 相对偏移（须 4 字节对齐）
@param out_word 输出
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_ReadMetaWord(uint32_t offset, uint32_t *out_word);

/**
@brief 编程 Meta 页内一个小端 uint32（擦后写 / 已匹配则成功）
@param offset 相对偏移（须 4 字节对齐）
@param value 目标字
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_ProgramMetaWord(uint32_t offset, uint32_t value);

/**
@brief 擦除 App 槽（ota_flash_app_base 起 ota_app_image_max 字节）
@note 不触碰 Boot / Meta。主机为 RAM 镜像；目标板桩未接线。
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_EraseApp(void);

/**
@brief 向 App 绝对地址编程字节流
@param abs_addr 绝对 Flash 地址（须落在 App 槽）
@param data 源数据
@param len 字节数
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_ProgramApp(uint32_t abs_addr, const uint8_t *data, size_t len);

/**
@brief 从 App 绝对地址读出
@param abs_addr 绝对地址
@param out 输出
@param len 长度
@return FMC_OK 或负错误码
*/
FmcStatus_t Fmc_ReadApp(uint32_t abs_addr, uint8_t *out, size_t len);

/**
@brief 比较 App 区与期望缓冲
@param abs_addr 绝对地址
@param data 期望
@param len 长度
@return FMC_OK 匹配
*/
FmcStatus_t Fmc_CompareApp(uint32_t abs_addr, const uint8_t *data, size_t len);

#if defined(FMC_HOST_MOCK) || defined(BOARD_GT32_HOST_MOCK)
/**
@brief 主机：复位 Meta 页与 App 槽镜像为全 0xFF，并清注入/计数
*/
void FmcMock_Reset(void);

/**
@brief 主机：下一次 EraseMetaPage 失败一次，随后恢复
*/
void FmcMock_InjectFailEraseOnce(void);

/**
@brief 主机：每次 Meta 擦除均失败（直至 Reset / ClearInject）
*/
void FmcMock_InjectFailEraseAlways(void);

/**
@brief 主机：编程若覆盖到 offset 则失败一次，随后恢复
@param offset 触发失败的相对偏移
*/
void FmcMock_InjectFailProgramAt(uint32_t offset);

/**
@brief 主机：编程若覆盖到 offset 则每次失败
@param offset 触发失败的相对偏移
*/
void FmcMock_InjectFailProgramAtAlways(uint32_t offset);

/**
@brief 主机：清除失败注入
*/
void FmcMock_ClearInject(void);

/**
@brief 主机：将 IMAGE_READY 字写成半写值（不经 Program API）
@param partial_value 非 0 且非 0xFFFFFFFF 的半写样例
*/
void FmcMock_InjectHalfWriteReady(uint32_t partial_value);

/**
@brief 主机：将 MATE_OTA_FAIL 字写成半写/已置位值
@param value 任意非 0xFFFFFFFF
*/
void FmcMock_InjectFailFlag(uint32_t value);

/**
@brief 主机：直接写入页镜像字节（测试装载已提交记录）
@param offset 相对偏移
@param data 数据
@param len 长度
*/
void FmcMock_LoadBytes(uint32_t offset, const uint8_t *data, size_t len);

/**
@brief 主机：累计 Meta 擦除次数
*/
size_t FmcMock_EraseCount(void);

/**
@brief 主机：累计 ProgramMeta / ProgramMetaWord 调用次数
*/
size_t FmcMock_ProgramCount(void);

/**
@brief 主机：页镜像只读指针（1024 字节）
*/
const uint8_t *FmcMock_PagePtr(void);

/**
@brief 主机：App 槽擦除次数
*/
size_t FmcMock_AppEraseCount(void);

/**
@brief 主机：App 编程调用次数
*/
size_t FmcMock_AppProgramCount(void);

/**
@brief 主机：最近一次 App 擦除的事件序号（相对 LastGood 先后）
*/
uint32_t FmcMock_AppEraseSeq(void);

/**
@brief 主机：下一次 Fmc_EraseApp 失败一次
*/
void FmcMock_InjectFailAppEraseOnce(void);

/**
@brief 主机：装载 App 镜像字节（相对 App 基址偏移）
@param offset 相对 ota_flash_app_base
@param data 数据
@param len 长度
*/
void FmcMock_LoadAppBytes(uint32_t offset, const uint8_t *data, size_t len);

/**
@brief 主机：App 镜像只读指针（ota_app_image_max 字节）
*/
const uint8_t *FmcMock_AppPtr(void);
#endif /* FMC_HOST_MOCK || BOARD_GT32_HOST_MOCK */

#endif /* CARDREADER_FMC_H */
