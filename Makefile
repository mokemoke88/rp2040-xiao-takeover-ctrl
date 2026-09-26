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
# 同じテストを 3 つの構成でビルドして実行する
#   TEST_LB : 仕様どおりの定数、同着時 LB 優先(既定)
#   TEST_RB : 同着時 RB 優先
#   TEST_ALT: タイミング定数を仕様値と違う値にした構成。テストの時刻が定数から計算されていて、
#             定数を変えても追従することを確かめる(値は takeover_config.h の前提を満たす範囲で選んでいる)
ALT_TIMING = -DTAKEOVER_T_TOGGLE_MS=700u -DTAKEOVER_T_MANUAL_MS=1500u -DTAKEOVER_T_PULSE_MS=50u -DTAKEOVER_T_SEL_DELAY_MS=3u
TEST_LB = $(HOST_BUILD)/test_takeover_logic$(EXE)
TEST_RB = $(HOST_BUILD)/test_takeover_logic_rb$(EXE)
TEST_ALT = $(HOST_BUILD)/test_takeover_logic_alt$(EXE)
TESTS = $(TEST_LB) $(TEST_RB) $(TEST_ALT)
TEST_DEPS = tests/test_takeover_logic.c src/takeover_logic.c $(UNITY_DIR)/unity.c include/takeover_logic.h include/takeover_config.h
TEST_SRCS = tests/test_takeover_logic.c src/takeover_logic.c $(UNITY_DIR)/unity.c

.PHONY: all test format lint firmware clean

all: test

test: $(TESTS)
	./$(TEST_LB)
	./$(TEST_RB)
	./$(TEST_ALT)

# takeover_logic.c だけをホストでビルドする(Pico SDK に依存しないため)
$(TEST_LB): $(TEST_DEPS) | $(HOST_BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

$(TEST_RB): $(TEST_DEPS) | $(HOST_BUILD)
	$(CC) $(CFLAGS) -DTAKEOVER_TIE_PREFER_LB=0 -o $@ $(TEST_SRCS)

$(TEST_ALT): $(TEST_DEPS) | $(HOST_BUILD)
	$(CC) $(CFLAGS) $(ALT_TIMING) -o $@ $(TEST_SRCS)

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
