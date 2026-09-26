# SPDX-License-Identifier: MIT
#
# ホスト(PC)側の作業: make test / make format / make lint
# ファームウェアのビルド:  make firmware(CMake + Ninja を呼び出す)

CC = gcc
UNITY_DIR = third_party/unity
HOST_BUILD = build-host
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Iinclude -I$(UNITY_DIR)

# ファームウェアのビルド設定(make firmware BUILD_DIR=build-nowdt CMAKE_ARGS=-DTAKEOVER_ENABLE_WATCHDOG=OFF のように上書きできる)
BUILD_DIR = build
PICO_BOARD = seeed_xiao_rp2040
CMAKE_ARGS =

# Windows の gcc は実行ファイルに .exe を付けるので、ターゲット名もそれに合わせる
# (合わせないと make がターゲットを見つけられず、毎回再ビルドになる)
ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif

FORMAT_FILES = $(wildcard include/*.h src/*.c tests/*.c)
# 同着時の優先を LB(既定)と RB の両方でテストする
TEST_LB = $(HOST_BUILD)/test_takeover_logic$(EXE)
TEST_RB = $(HOST_BUILD)/test_takeover_logic_rb$(EXE)
TESTS = $(TEST_LB) $(TEST_RB)
TEST_DEPS = tests/test_takeover_logic.c src/takeover_logic.c $(UNITY_DIR)/unity.c include/takeover_logic.h include/takeover_config.h
TEST_SRCS = tests/test_takeover_logic.c src/takeover_logic.c $(UNITY_DIR)/unity.c

.PHONY: all test format lint firmware clean

all: test

test: $(TESTS)
	./$(TEST_LB)
	./$(TEST_RB)

# takeover_logic.c だけをホストでビルドする(Pico SDK に依存しないため)
$(TEST_LB): $(TEST_DEPS) | $(HOST_BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

$(TEST_RB): $(TEST_DEPS) | $(HOST_BUILD)
	$(CC) $(CFLAGS) -DTAKEOVER_TIE_PREFER_LB=0 -o $@ $(TEST_SRCS)

$(HOST_BUILD):
	mkdir -p $@

format:
	clang-format -i $(FORMAT_FILES)

lint:
	clang-format --dry-run --Werror $(FORMAT_FILES)

firmware:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DPICO_BOARD=$(PICO_BOARD) $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(HOST_BUILD)
