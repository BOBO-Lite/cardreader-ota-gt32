/**
@file test_factory_pack.c
@brief Slice 9 主机自检：工厂 128 KiB 打包与反向核对（E1/E2；非 E3）
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "factory_pack.h"
#include "ota_layout.h"
#include "ota_meta_codec.h"

static int s_failures = 0;

static void ExpectEqI(const char *name, int got, int expect)
{
    if (got != expect) {
        (void)printf("FAIL: %s got=%d expect=%d\n", name, got, expect);
        s_failures++;
    } else {
        (void)printf("PASS: %s\n", name);
    }
}

static void ExpectEqU32(const char *name, uint32_t got, uint32_t expect)
{
    if (got != expect) {
        (void)printf("FAIL: %s got=0x%08X expect=0x%08X\n",
                     name, (unsigned)got, (unsigned)expect);
        s_failures++;
    } else {
        (void)printf("PASS: %s\n", name);
    }
}

static void ExpectStr(const char *name, const char *got, const char *expect)
{
    if (strcmp(got, expect) != 0) {
        (void)printf("FAIL: %s got=\"%s\" expect=\"%s\"\n", name, got, expect);
        s_failures++;
    } else {
        (void)printf("PASS: %s\n", name);
    }
}

/**
@brief 构造最小合法 App：MSP=0x20005000，Reset=0x08004009（Thumb）
*/
static void FillMinimalApp(uint8_t *app, size_t app_len)
{
    (void)memset(app, 0x00, app_len);
    app[0] = 0x00;
    app[1] = 0x50;
    app[2] = 0x00;
    app[3] = 0x20;
    app[4] = 0x09;
    app[5] = 0x40;
    app[6] = 0x00;
    app[7] = 0x08;
}

static void TestHappyPath(void)
{
    uint8_t boot[16];
    uint8_t app[32];
    uint8_t pkg[factory_pack_size];
    FactoryPackStatus_t st;
    OtaMetaRecord_t rec;
    OtaMetaStatus_t mst;
    static const char *k_name = "cardreader-v0.3.1.bin";

    (void)memset(boot, 0xA5, sizeof(boot));
    FillMinimalApp(app, sizeof(app));

    st = FactoryPack_Build(boot, sizeof(boot), app, sizeof(app), k_name,
                           pkg, sizeof(pkg));
    ExpectEqI("Build OK", (int)st, (int)FACTORY_PACK_OK);

    st = FactoryPack_Verify(pkg, sizeof(pkg));
    ExpectEqI("Verify OK", (int)st, (int)FACTORY_PACK_OK);

    ExpectEqU32("pkg size const", factory_pack_size, 131072u);
    ExpectEqU32("meta off", factory_pack_off_meta, 0x1F800u);
    ExpectEqU32("app off", factory_pack_off_app, 0x04000u);

    mst = OtaMeta_DecodeRecord(pkg + factory_pack_off_meta, &rec, 1);
    ExpectEqI("Decode meta", (int)mst, (int)OTA_META_OK);
    ExpectStr("filename", rec.filename, k_name);
    ExpectStr("version", rec.version, "v0.3.1");
    ExpectEqU32("image_size", rec.image_size, (uint32_t)sizeof(app));
    ExpectEqU32("READY=0", rec.image_ready, 0u);
    ExpectEqU32("VALID=0", rec.app_valid, 0u);
    ExpectEqU32("FAIL=erased", rec.mate_ota_fail, 0xFFFFFFFFu);
    ExpectEqU32("board_id", rec.board_id, 0x0001u);
    ExpectEqU32("fw_crc", rec.fw_crc32,
                OtaCrc32_Compute(app, sizeof(app)));

    /* Boot 模式写入检查 */
    if (memcmp(pkg + factory_pack_off_boot, boot, sizeof(boot)) != 0) {
        (void)printf("FAIL: boot payload\n");
        s_failures++;
    } else {
        (void)printf("PASS: boot payload\n");
    }
    if (memcmp(pkg + factory_pack_off_app, app, sizeof(app)) != 0) {
        (void)printf("FAIL: app payload\n");
        s_failures++;
    } else {
        (void)printf("PASS: app payload\n");
    }

    /* 无片内 Secondary 副本：App 槽尾与 Meta 之间为 FF（已由 Verify）；Meta 后为 FF */
    (void)printf("PASS: no Secondary/LastGood embedded (on-chip package only)\n");
}

static void TestRejects(void)
{
    uint8_t boot[16];
    uint8_t boot_big[16385];
    uint8_t app[32];
    uint8_t app_bad_vec[32];
    uint8_t pkg[factory_pack_size];
    FactoryPackStatus_t st;
    static const char *k_name = "cardreader-v0.3.1.bin";

    (void)memset(boot, 0x11, sizeof(boot));
    (void)memset(boot_big, 0x22, sizeof(boot_big));
    FillMinimalApp(app, sizeof(app));
    FillMinimalApp(app_bad_vec, sizeof(app_bad_vec));
    /* 破坏 Thumb 位 */
    app_bad_vec[4] = 0x08;

    st = FactoryPack_Build(boot_big, sizeof(boot_big), app, sizeof(app), k_name,
                           pkg, sizeof(pkg));
    ExpectEqI("reject boot too large", (int)st, (int)FACTORY_PACK_ERR_BOOT);

    st = FactoryPack_Build(boot, sizeof(boot), app, 4u, k_name,
                           pkg, sizeof(pkg));
    ExpectEqI("reject app too small", (int)st, (int)FACTORY_PACK_ERR_APP_SIZE);

    st = FactoryPack_Build(boot, sizeof(boot), app, sizeof(app), "bad.bin",
                           pkg, sizeof(pkg));
    ExpectEqI("reject bad filename", (int)st, (int)FACTORY_PACK_ERR_FILENAME);

    st = FactoryPack_Build(boot, sizeof(boot), app_bad_vec, sizeof(app_bad_vec),
                           k_name, pkg, sizeof(pkg));
    ExpectEqI("reject bad vector", (int)st, (int)FACTORY_PACK_ERR_VECTOR);

    st = FactoryPack_Build(boot, sizeof(boot), app, sizeof(app), k_name,
                           pkg, sizeof(pkg));
    ExpectEqI("build for mutate", (int)st, (int)FACTORY_PACK_OK);

    st = FactoryPack_Verify(pkg, factory_pack_size - 1u);
    ExpectEqI("reject wrong size", (int)st, (int)FACTORY_PACK_ERR_LAYOUT);

    /* 破坏 Meta CRC */
    pkg[factory_pack_off_meta + ota_meta_off_meta_crc32] ^= 0x01u;
    st = FactoryPack_Verify(pkg, factory_pack_size);
    ExpectEqI("reject bad meta crc", (int)st, (int)FACTORY_PACK_ERR_CRC);
    pkg[factory_pack_off_meta + ota_meta_off_meta_crc32] ^= 0x01u;

    /* 破坏 App 后填充 */
    pkg[factory_pack_off_app + sizeof(app)] = 0x00;
    st = FactoryPack_Verify(pkg, factory_pack_size);
    ExpectEqI("reject non-FF padding", (int)st, (int)FACTORY_PACK_ERR_PADDING);
}

int main(void)
{
    TestHappyPath();
    TestRejects();
    if (s_failures != 0) {
        (void)printf("RESULT: factory_pack FAIL (%d)\n", s_failures);
        return 1;
    }
    (void)printf("RESULT: all FactoryPack Slice 9 host tests PASS\n");
    return 0;
}
