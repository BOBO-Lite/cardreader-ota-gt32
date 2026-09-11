/**
@file fmc.c
@brief Meta 页 + App 区 FMC 抽象（主机 RAM 镜像；目标板桩未接线）
@note 禁止 Chip Erase；只擦 Meta 第一页与 App 槽。实机尚未验证（非 E3）。
*/

#include "fmc.h"

#include <string.h>

#include "ota_layout.h"

#if defined(BOARD_GT32_HOST_MOCK)
#include "board_gt32.h"
#endif

#if defined(FMC_HOST_MOCK) || defined(BOARD_GT32_HOST_MOCK)

static uint8_t s_page[ota_flash_meta_page_size];
static uint8_t s_app[ota_app_image_max];
static size_t s_erase_count;
static size_t s_program_count;
static size_t s_app_erase_count;
static size_t s_app_program_count;
static uint32_t s_app_erase_seq;
static int s_fail_erase_once;
static int s_fail_erase_always;
static int s_fail_program_once;
static int s_fail_program_always;
static uint32_t s_fail_program_offset;
static int s_fail_app_erase_once;

static int FmcMock_RangeOk(uint32_t offset, size_t len)
{
    if (len == 0u) {
        return 0;
    }
    if (offset >= ota_flash_meta_page_size) {
        return 0;
    }
    if (len > (ota_flash_meta_page_size - offset)) {
        return 0;
    }
    return 1;
}

static int FmcMock_AppRangeOk(uint32_t abs_addr, size_t len)
{
    uint32_t end;

    if (len == 0u) {
        return 0;
    }
    if (abs_addr < ota_flash_app_base) {
        return 0;
    }
    if ((abs_addr - ota_flash_app_base) >= ota_app_image_max) {
        return 0;
    }
    if (len > (ota_app_image_max - (abs_addr - ota_flash_app_base))) {
        return 0;
    }
    end = abs_addr + (uint32_t)len - 1u;
    if (end < abs_addr || end > ota_flash_app_end) {
        return 0;
    }
    return 1;
}

/**
@brief NOR 语义：仅当目标可由当前字通过清位得到，或已相等
*/
static int FmcMock_CanProgramByte(uint8_t current, uint8_t want)
{
    if (current == want) {
        return 1;
    }
    if ((uint8_t)(current & want) != want) {
        return 0;
    }
    return 1;
}

void FmcMock_Reset(void)
{
    (void)memset(s_page, 0xFF, sizeof(s_page));
    (void)memset(s_app, 0xFF, sizeof(s_app));
    s_erase_count = 0u;
    s_program_count = 0u;
    s_app_erase_count = 0u;
    s_app_program_count = 0u;
    s_app_erase_seq = 0u;
    s_fail_erase_once = 0;
    s_fail_erase_always = 0;
    s_fail_program_once = 0;
    s_fail_program_always = 0;
    s_fail_program_offset = 0u;
    s_fail_app_erase_once = 0;
}

void FmcMock_InjectFailEraseOnce(void)
{
    s_fail_erase_once = 1;
}

void FmcMock_InjectFailEraseAlways(void)
{
    s_fail_erase_always = 1;
}

void FmcMock_InjectFailProgramAt(uint32_t offset)
{
    s_fail_program_once = 1;
    s_fail_program_offset = offset;
}

void FmcMock_InjectFailProgramAtAlways(uint32_t offset)
{
    s_fail_program_always = 1;
    s_fail_program_offset = offset;
}

void FmcMock_ClearInject(void)
{
    s_fail_erase_once = 0;
    s_fail_erase_always = 0;
    s_fail_program_once = 0;
    s_fail_program_always = 0;
    s_fail_app_erase_once = 0;
}

void FmcMock_InjectHalfWriteReady(uint32_t partial_value)
{
    s_page[ota_meta_off_image_ready + 0u] = (uint8_t)(partial_value & 0xFFu);
    s_page[ota_meta_off_image_ready + 1u] =
        (uint8_t)((partial_value >> 8) & 0xFFu);
    s_page[ota_meta_off_image_ready + 2u] =
        (uint8_t)((partial_value >> 16) & 0xFFu);
    s_page[ota_meta_off_image_ready + 3u] =
        (uint8_t)((partial_value >> 24) & 0xFFu);
}

void FmcMock_InjectFailFlag(uint32_t value)
{
    s_page[ota_meta_off_mate_ota_fail + 0u] = (uint8_t)(value & 0xFFu);
    s_page[ota_meta_off_mate_ota_fail + 1u] = (uint8_t)((value >> 8) & 0xFFu);
    s_page[ota_meta_off_mate_ota_fail + 2u] = (uint8_t)((value >> 16) & 0xFFu);
    s_page[ota_meta_off_mate_ota_fail + 3u] = (uint8_t)((value >> 24) & 0xFFu);
}

void FmcMock_LoadBytes(uint32_t offset, const uint8_t *data, size_t len)
{
    if (data == NULL || FmcMock_RangeOk(offset, len) == 0) {
        return;
    }
    (void)memcpy(&s_page[offset], data, len);
}

size_t FmcMock_EraseCount(void)
{
    return s_erase_count;
}

size_t FmcMock_ProgramCount(void)
{
    return s_program_count;
}

const uint8_t *FmcMock_PagePtr(void)
{
    return s_page;
}

size_t FmcMock_AppEraseCount(void)
{
    return s_app_erase_count;
}

size_t FmcMock_AppProgramCount(void)
{
    return s_app_program_count;
}

uint32_t FmcMock_AppEraseSeq(void)
{
    return s_app_erase_seq;
}

void FmcMock_InjectFailAppEraseOnce(void)
{
    s_fail_app_erase_once = 1;
}

void FmcMock_LoadAppBytes(uint32_t offset, const uint8_t *data, size_t len)
{
    if (data == NULL || offset >= ota_app_image_max) {
        return;
    }
    if (len > (ota_app_image_max - offset)) {
        return;
    }
    (void)memcpy(&s_app[offset], data, len);
}

const uint8_t *FmcMock_AppPtr(void)
{
    return s_app;
}

FmcStatus_t Fmc_EraseMetaPage(void)
{
    if (s_fail_erase_always != 0) {
        s_erase_count++;
        return FMC_ERR_HARDWARE;
    }
    if (s_fail_erase_once != 0) {
        s_fail_erase_once = 0;
        s_erase_count++;
        return FMC_ERR_HARDWARE;
    }
    (void)memset(s_page, 0xFF, sizeof(s_page));
    s_erase_count++;
    return FMC_OK;
}

FmcStatus_t Fmc_ProgramMeta(uint32_t offset, const uint8_t *data, size_t len)
{
    size_t i;

    if (data == NULL) {
        return FMC_ERR_NULL;
    }
    if (FmcMock_RangeOk(offset, len) == 0) {
        return FMC_ERR_RANGE;
    }

    s_program_count++;

    if ((s_fail_program_once != 0 || s_fail_program_always != 0)
        && offset <= s_fail_program_offset
        && (offset + len) > s_fail_program_offset) {
        if (s_fail_program_once != 0) {
            s_fail_program_once = 0;
        }
        return FMC_ERR_HARDWARE;
    }

    for (i = 0u; i < len; i++) {
        if (FmcMock_CanProgramByte(s_page[offset + i], data[i]) == 0) {
            return FMC_ERR_HARDWARE;
        }
    }
    for (i = 0u; i < len; i++) {
        s_page[offset + i] = (uint8_t)(s_page[offset + i] & data[i]);
    }
    return FMC_OK;
}

FmcStatus_t Fmc_ReadMeta(uint32_t offset, uint8_t *out, size_t len)
{
    if (out == NULL) {
        return FMC_ERR_NULL;
    }
    if (FmcMock_RangeOk(offset, len) == 0) {
        return FMC_ERR_RANGE;
    }
    (void)memcpy(out, &s_page[offset], len);
    return FMC_OK;
}

FmcStatus_t Fmc_CompareMeta(uint32_t offset, const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return FMC_ERR_NULL;
    }
    if (FmcMock_RangeOk(offset, len) == 0) {
        return FMC_ERR_RANGE;
    }
    if (memcmp(&s_page[offset], data, len) != 0) {
        return FMC_ERR_HARDWARE;
    }
    return FMC_OK;
}

FmcStatus_t Fmc_ReadMetaWord(uint32_t offset, uint32_t *out_word)
{
    if (out_word == NULL) {
        return FMC_ERR_NULL;
    }
    if ((offset & 3u) != 0u || FmcMock_RangeOk(offset, 4u) == 0) {
        return FMC_ERR_PARAM;
    }
    *out_word = ((uint32_t)s_page[offset])
              | ((uint32_t)s_page[offset + 1u] << 8)
              | ((uint32_t)s_page[offset + 2u] << 16)
              | ((uint32_t)s_page[offset + 3u] << 24);
    return FMC_OK;
}

FmcStatus_t Fmc_ProgramMetaWord(uint32_t offset, uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    return Fmc_ProgramMeta(offset, bytes, 4u);
}

FmcStatus_t Fmc_EraseApp(void)
{
    s_app_erase_count++;
#if defined(BOARD_GT32_HOST_MOCK)
    s_app_erase_seq = BoardGt32Mock_BumpEventSeq();
#else
    s_app_erase_seq++;
#endif
    if (s_fail_app_erase_once != 0) {
        s_fail_app_erase_once = 0;
        return FMC_ERR_HARDWARE;
    }
    (void)memset(s_app, 0xFF, sizeof(s_app));
    return FMC_OK;
}

FmcStatus_t Fmc_ProgramApp(uint32_t abs_addr, const uint8_t *data, size_t len)
{
    size_t i;
    uint32_t off;

    if (data == NULL) {
        return FMC_ERR_NULL;
    }
    if (FmcMock_AppRangeOk(abs_addr, len) == 0) {
        return FMC_ERR_RANGE;
    }
    s_app_program_count++;
    off = abs_addr - ota_flash_app_base;
    for (i = 0u; i < len; i++) {
        if (FmcMock_CanProgramByte(s_app[off + i], data[i]) == 0) {
            return FMC_ERR_HARDWARE;
        }
    }
    for (i = 0u; i < len; i++) {
        s_app[off + i] = (uint8_t)(s_app[off + i] & data[i]);
    }
    return FMC_OK;
}

FmcStatus_t Fmc_ReadApp(uint32_t abs_addr, uint8_t *out, size_t len)
{
    uint32_t off;

    if (out == NULL) {
        return FMC_ERR_NULL;
    }
    if (FmcMock_AppRangeOk(abs_addr, len) == 0) {
        return FMC_ERR_RANGE;
    }
    off = abs_addr - ota_flash_app_base;
    (void)memcpy(out, &s_app[off], len);
    return FMC_OK;
}

FmcStatus_t Fmc_CompareApp(uint32_t abs_addr, const uint8_t *data, size_t len)
{
    uint32_t off;

    if (data == NULL) {
        return FMC_ERR_NULL;
    }
    if (FmcMock_AppRangeOk(abs_addr, len) == 0) {
        return FMC_ERR_RANGE;
    }
    off = abs_addr - ota_flash_app_base;
    if (memcmp(&s_app[off], data, len) != 0) {
        return FMC_ERR_HARDWARE;
    }
    return FMC_OK;
}

#else /* 目标板：未接线桩 */

FmcStatus_t Fmc_EraseMetaPage(void)
{
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_ProgramMeta(uint32_t offset, const uint8_t *data, size_t len)
{
    (void)offset;
    (void)data;
    (void)len;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_ReadMeta(uint32_t offset, uint8_t *out, size_t len)
{
    (void)offset;
    (void)out;
    (void)len;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_CompareMeta(uint32_t offset, const uint8_t *data, size_t len)
{
    (void)offset;
    (void)data;
    (void)len;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_ReadMetaWord(uint32_t offset, uint32_t *out_word)
{
    (void)offset;
    (void)out_word;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_ProgramMetaWord(uint32_t offset, uint32_t value)
{
    (void)offset;
    (void)value;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_EraseApp(void)
{
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_ProgramApp(uint32_t abs_addr, const uint8_t *data, size_t len)
{
    (void)abs_addr;
    (void)data;
    (void)len;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_ReadApp(uint32_t abs_addr, uint8_t *out, size_t len)
{
    (void)abs_addr;
    (void)out;
    (void)len;
    return FMC_ERR_HARDWARE;
}

FmcStatus_t Fmc_CompareApp(uint32_t abs_addr, const uint8_t *data, size_t len)
{
    (void)abs_addr;
    (void)data;
    (void)len;
    return FMC_ERR_HARDWARE;
}

#endif /* FMC_HOST_MOCK || BOARD_GT32_HOST_MOCK */
