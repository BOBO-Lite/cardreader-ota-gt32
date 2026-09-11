# CardReader GT32 OTA — host tests (Slice 1–9)
# Slice 9 = factory 128 KiB pack tool（AC-YM-19；主机 E1/E2；禁 YMODEM；非 E3）
CC       ?= gcc
CFLAGS   ?= -std=c99 -Wall -Wextra -Werror -O0 -g
CPPFLAGS  = -Iinclude
BUILD     = build

META_SRC      = src/ota_meta_codec.c
META_TEST_SRC = tests/test_ota_meta_codec.c
META_BIN      = $(BUILD)/test_ota_meta_codec

GT32_SRC      = src/gt32_raw.c src/board_gt32.c
GT32_TEST_SRC = tests/test_gt32_raw.c
GT32_BIN      = $(BUILD)/test_gt32_raw
GT32_CPPFLAGS = $(CPPFLAGS) -DBOARD_GT32_HOST_MOCK

BOOT_COMMON   = src/boot.c src/boot_bkp.c src/drv_ymodem.c src/drv_usart.c \
                src/gt32_raw.c src/board_gt32.c src/ota_meta_codec.c \
                src/ota_meta.c src/fmc.c src/ota_image.c

COMMIT_SRC      = $(BOOT_COMMON)
COMMIT_TEST_SRC = tests/test_ota_meta_commit.c
COMMIT_BIN      = $(BUILD)/test_ota_meta_commit
COMMIT_CPPFLAGS = $(CPPFLAGS) -DBOARD_GT32_HOST_MOCK -DFMC_HOST_MOCK

BOOT_SRC      = $(BOOT_COMMON)
BOOT_TEST_SRC = tests/test_boot_ota.c
BOOT_BIN      = $(BUILD)/test_boot_ota
BOOT_CPPFLAGS = $(CPPFLAGS) -DBOARD_GT32_HOST_MOCK -DFMC_HOST_MOCK

INSTALL_SRC      = $(BOOT_COMMON)
INSTALL_TEST_SRC = tests/test_ota_install.c
INSTALL_BIN      = $(BUILD)/test_ota_install
INSTALL_CPPFLAGS = $(CPPFLAGS) -DBOARD_GT32_HOST_MOCK -DFMC_HOST_MOCK

SELECT_SRC      = $(BOOT_COMMON) src/main.c
SELECT_TEST_SRC = tests/test_boot_select.c
SELECT_BIN      = $(BUILD)/test_boot_select
SELECT_CPPFLAGS = $(CPPFLAGS) -DBOARD_GT32_HOST_MOCK -DFMC_HOST_MOCK

APP_OTA_SRC      = src/app_ota.c src/boot_bkp.c
APP_OTA_TEST_SRC = tests/test_app_ota.c
APP_OTA_BIN      = $(BUILD)/test_app_ota
APP_OTA_CPPFLAGS = $(CPPFLAGS) -DBOARD_GT32_HOST_MOCK

FACTORY_SRC      = src/factory_pack.c src/ota_meta_codec.c
FACTORY_TEST_SRC = tests/test_factory_pack.c
FACTORY_BIN      = $(BUILD)/test_factory_pack
FACTORY_TOOL     = $(BUILD)/factory_pack



.PHONY: all test clean check_no_chip_erase check_transfer_no_meta_commit check_app_ota_no_modbus check_factory_no_ymodem

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(META_BIN): $(META_SRC) $(META_TEST_SRC) include/ota_layout.h include/ota_meta_codec.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(META_TEST_SRC) $(META_SRC)

$(GT32_BIN): $(GT32_SRC) $(GT32_TEST_SRC) include/gt32_raw.h include/board_gt32.h include/ota_layout.h | $(BUILD)
	$(CC) $(CFLAGS) $(GT32_CPPFLAGS) -o $@ $(GT32_TEST_SRC) $(GT32_SRC)

$(COMMIT_BIN): $(COMMIT_SRC) $(COMMIT_TEST_SRC) include/ota_meta.h include/fmc.h \
               include/boot.h include/boot_bkp.h include/ota_meta_codec.h include/ota_layout.h include/ota_image.h | $(BUILD)
	$(CC) $(CFLAGS) $(COMMIT_CPPFLAGS) -o $@ $(COMMIT_TEST_SRC) $(COMMIT_SRC)

$(BOOT_BIN): $(BOOT_SRC) $(BOOT_TEST_SRC) include/boot.h include/boot_bkp.h include/drv_ymodem.h include/drv_usart.h \
             include/gt32_raw.h include/board_gt32.h include/ota_layout.h include/ota_meta_codec.h \
             include/ota_meta.h include/fmc.h include/ota_image.h | $(BUILD)
	$(CC) $(CFLAGS) $(BOOT_CPPFLAGS) -o $@ $(BOOT_TEST_SRC) $(BOOT_SRC)

$(INSTALL_BIN): $(INSTALL_SRC) $(INSTALL_TEST_SRC) include/boot.h include/boot_bkp.h include/ota_image.h \
                include/ota_meta.h include/fmc.h include/gt32_raw.h include/ota_layout.h | $(BUILD)
	$(CC) $(CFLAGS) $(INSTALL_CPPFLAGS) -o $@ $(INSTALL_TEST_SRC) $(INSTALL_SRC)

$(SELECT_BIN): $(SELECT_SRC) $(SELECT_TEST_SRC) include/boot.h include/boot_bkp.h include/ota_meta.h \
               include/fmc.h include/ota_layout.h include/ota_image.h | $(BUILD)
	$(CC) $(CFLAGS) $(SELECT_CPPFLAGS) -o $@ $(SELECT_TEST_SRC) $(SELECT_SRC)

$(APP_OTA_BIN): $(APP_OTA_SRC) $(APP_OTA_TEST_SRC) include/app_ota.h include/boot_bkp.h | $(BUILD)
	$(CC) $(CFLAGS) $(APP_OTA_CPPFLAGS) -o $@ $(APP_OTA_TEST_SRC) $(APP_OTA_SRC)

$(FACTORY_BIN): $(FACTORY_SRC) $(FACTORY_TEST_SRC) include/factory_pack.h include/ota_layout.h include/ota_meta_codec.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(FACTORY_TEST_SRC) $(FACTORY_SRC)

$(FACTORY_TOOL): $(FACTORY_SRC) tools/factory_pack_main.c include/factory_pack.h include/ota_layout.h include/ota_meta_codec.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tools/factory_pack_main.c $(FACTORY_SRC)



# 生产源中不得出现 Chip Erase 操作码常量或符号
check_no_chip_erase:
	@python3 -c "import pathlib,re,sys;\
files=['include/gt32_raw.h','include/board_gt32.h','src/gt32_raw.c','src/board_gt32.c',\
'include/boot.h','src/boot.c','include/boot_bkp.h','src/boot_bkp.c','src/main.c',\
'include/drv_ymodem.h','src/drv_ymodem.c',\
'include/drv_usart.h','src/drv_usart.c','include/fmc.h','src/fmc.c',\
'include/ota_meta.h','src/ota_meta.c','include/ota_image.h','src/ota_image.c',\
'include/app_ota.h','src/app_ota.c','include/factory_pack.h','src/factory_pack.c'];\
pat=re.compile(r'(?<![0-9A-Fa-f])0x(60|C7)(?![0-9A-Fa-f])|gt32_cmd_chip_erase|ChipErase|CHIP_ERASE');\
bad=[];\
[bad.extend([(f,i+1,l.rstrip()) for i,l in enumerate(pathlib.Path(f).read_text().splitlines()) if pat.search(l)]) for f in files];\
sys.exit('FAIL: Chip Erase found:\n'+'\n'.join(f'{a}:{b}:{c}' for a,b,c in bad)) if bad else print('PASS: no Chip Erase opcodes/symbols in gt32/boot/fmc/meta/image production sources')"

# 传输模块不得引用 OtaMeta_Commit*；boot 的 HandleData/EraseSecondary/Header 亦不得
check_transfer_no_meta_commit:
	@python3 scripts/check_transfer_no_meta_commit.py

# AppOta 不得含 Modbus FC/寄存器实现（注释中的“不做 Modbus”除外）
check_app_ota_no_modbus:
	@python3 scripts/check_app_ota_no_modbus.py

check_factory_no_ymodem:
	@python3 scripts/check_factory_no_ymodem.py


test: $(META_BIN) $(GT32_BIN) $(COMMIT_BIN) $(BOOT_BIN) $(INSTALL_BIN) $(SELECT_BIN) $(APP_OTA_BIN) $(FACTORY_BIN) $(FACTORY_TOOL) check_no_chip_erase check_transfer_no_meta_commit check_app_ota_no_modbus check_factory_no_ymodem
	$(META_BIN)
	$(GT32_BIN)
	$(COMMIT_BIN)
	$(BOOT_BIN)
	$(INSTALL_BIN)
	$(SELECT_BIN)
	$(APP_OTA_BIN)
	$(FACTORY_BIN)

clean:
	rm -rf $(BUILD)
