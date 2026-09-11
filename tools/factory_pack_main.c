/**
@file factory_pack_main.c
@brief 出厂 128 KiB 打包 CLI（pack / verify）；禁止经 YMODEM 发送
@note 用法：
  factory_pack pack -b boot.bin -a cardreader-vX.Y.Z.bin -o factory-….bin
  factory_pack verify factory-….bin
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "factory_pack.h"

/**
@brief 读入整个文件
@param path 路径
@param out_buf 输出指针（调用方 free）
@param out_len 输出长度
@return 0 成功；非 0 失败
*/
static int ReadAll(const char *path, uint8_t **out_buf, size_t *out_len)
{
    FILE *fp;
    long sz;
    uint8_t *buf;
    size_t n;

    if (path == NULL || out_buf == NULL || out_len == NULL) {
        return -1;
    }
    *out_buf = NULL;
    *out_len = 0u;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        (void)fprintf(stderr, "无法打开: %s\n", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz < 0) {
        (void)fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        (void)fclose(fp);
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL && sz != 0) {
        (void)fclose(fp);
        return -1;
    }
    n = fread(buf, 1, (size_t)sz, fp);
    (void)fclose(fp);
    if (n != (size_t)sz) {
        free(buf);
        return -1;
    }
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

/**
@brief 取路径 basename
@param path 路径
@return basename 指针（可能指向 path 内部）
*/
static const char *BaseName(const char *path)
{
    const char *p;
    const char *slash;

    if (path == NULL) {
        return "";
    }
    slash = path;
    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            slash = p + 1;
        }
    }
    return slash;
}

static void Usage(void)
{
    (void)fprintf(stderr,
        "用法:\n"
        "  factory_pack pack -b <boot.bin> -a <cardreader-v*.bin> -o <out.bin>\n"
        "  factory_pack verify <factory-*.bin>\n"
        "说明: 输出建议 factory-<app名>；禁止用 YMODEM 发送本包；不烧录板端。\n");
}

static int CmdPack(int argc, char **argv)
{
    const char *boot_path = NULL;
    const char *app_path = NULL;
    const char *out_path = NULL;
    uint8_t *boot = NULL;
    uint8_t *app = NULL;
    uint8_t *pkg = NULL;
    size_t boot_len = 0u;
    size_t app_len = 0u;
    FactoryPackStatus_t st;
    FILE *fp;
    int i;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 && (i + 1) < argc) {
            boot_path = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0 && (i + 1) < argc) {
            app_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && (i + 1) < argc) {
            out_path = argv[++i];
        } else {
            Usage();
            return 1;
        }
    }
    if (boot_path == NULL || app_path == NULL || out_path == NULL) {
        Usage();
        return 1;
    }

    if (ReadAll(boot_path, &boot, &boot_len) != 0
        || ReadAll(app_path, &app, &app_len) != 0) {
        free(boot);
        free(app);
        return 1;
    }

    pkg = (uint8_t *)malloc(factory_pack_size);
    if (pkg == NULL) {
        free(boot);
        free(app);
        return 1;
    }

    st = FactoryPack_Build(boot, boot_len, app, app_len, BaseName(app_path),
                           pkg, factory_pack_size);
    if (st != FACTORY_PACK_OK) {
        (void)fprintf(stderr, "打包失败: status=%d\n", (int)st);
        free(boot);
        free(app);
        free(pkg);
        return 1;
    }

    st = FactoryPack_Verify(pkg, factory_pack_size);
    if (st != FACTORY_PACK_OK) {
        (void)fprintf(stderr, "自检失败: status=%d\n", (int)st);
        free(boot);
        free(app);
        free(pkg);
        return 1;
    }

    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        (void)fprintf(stderr, "无法写入: %s\n", out_path);
        free(boot);
        free(app);
        free(pkg);
        return 1;
    }
    if (fwrite(pkg, 1, factory_pack_size, fp) != factory_pack_size) {
        (void)fclose(fp);
        free(boot);
        free(app);
        free(pkg);
        return 1;
    }
    (void)fclose(fp);

    (void)printf("OK: wrote %u bytes -> %s (勿经 YMODEM 发送；GT32 不在本包内)\n",
                 (unsigned)factory_pack_size, out_path);
    free(boot);
    free(app);
    free(pkg);
    return 0;
}

static int CmdVerify(const char *path)
{
    uint8_t *pkg = NULL;
    size_t len = 0u;
    FactoryPackStatus_t st;

    if (ReadAll(path, &pkg, &len) != 0) {
        return 1;
    }
    st = FactoryPack_Verify(pkg, len);
    free(pkg);
    if (st != FACTORY_PACK_OK) {
        (void)fprintf(stderr, "核对失败: status=%d\n", (int)st);
        return 1;
    }
    (void)printf("OK: verify PASS (%s)\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        Usage();
        return 1;
    }
    if (strcmp(argv[1], "pack") == 0) {
        return CmdPack(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "verify") == 0) {
        if (argc < 3) {
            Usage();
            return 1;
        }
        return CmdVerify(argv[2]);
    }
    Usage();
    return 1;
}
