# 轻量级智能跌倒检测监护系统 - 顶层 Makefile
# 基于 K230 RT-Smart AI 套件

# 项目名称
PROJECT_NAME := fall_detection
VERSION       := 1.0.0

# 工具链 (RISC-V GCC)
CROSS_COMPILE ?= riscv64-unknown-elf-
CC             := $(CROSS_COMPILE)gcc
AS             := $(CROSS_COMPILE)as
LD             := $(CROSS_COMPILE)ld
OBJCOPY        := $(CROSS_COMPILE)objcopy
SIZE           := $(CROSS_COMPILE)size

# RT-Thread Smart SDK 路径
RTT_DIR       ?= $(RTTHREAD_ROOT)
K230_SDK_DIR  ?= $(RTT_DIR)/libcpu/risc-v/t-head/k230

# 编译选项
CFLAGS  := -march=rv64imafdc -mabi=lp64d
CFLAGS  += -mcmodel=medany -msmall-data-limit=8
CFLAGS  += -O2 -g -Wall -Wextra
CFLAGS  += -Icommon/inc
CFLAGS  += -Iprocess_video/inc
CFLAGS  += -Iprocess_ai/inc
CFLAGS  += -Iprocess_manager/inc
CFLAGS  += -I$(RTT_DIR)/include
CFLAGS  += -I$(RTT_DIR)/components/lwp

LDFLAGS := -T link.lds
LDFLAGS += -L$(RTT_DIR)/lib -lrtt

# 源文件
COMMON_SRC := \
    common/src/ipc_protocol.c \
    common/src/config.c \
    common/src/log.c \
    common/src/pose_overlay.c

VIDEO_SRC := \
    process_video/main.c \
    process_video/src/video_capture.c \
    process_video/src/video_display.c \
    process_video/src/video_osd.c \
    process_video/src/video_rtsp.c \
    process_video/src/video_record.c

AI_SRC := \
    process_ai/main.c \
    process_ai/src/ai_engine.c \
    process_ai/src/fall_detect.c \
    process_ai/src/fall_alert.c \
    process_ai/src/fall_notify.c \
    process_ai/src/fall_storage.c

MANAGER_SRC := \
    process_manager/main.c \
    process_manager/src/sys_init.c \
    process_manager/src/key_handler.c \
    process_manager/src/event_log.c \
    process_manager/src/sys_monitor.c

# 对象文件
COMMON_OBJ := $(COMMON_SRC:.c=.o)
VIDEO_OBJ  := $(VIDEO_SRC:.c=.o)
AI_OBJ     := $(AI_SRC:.c=.o)
MANAGER_OBJ:= $(MANAGER_SRC:.c=.o)

ALL_OBJ := $(COMMON_OBJ) $(VIDEO_OBJ) $(AI_OBJ) $(MANAGER_OBJ)

# 目标
.PHONY: all clean video ai manager

all: $(PROJECT_NAME)

$(PROJECT_NAME): $(ALL_OBJ)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -o $@ $^
	@$(SIZE) $@
	@echo "Build complete: $(PROJECT_NAME) ($(VERSION))"

# 编译规则
%.o: %.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# 单独编译各进程 (用于开发调试)
video: $(COMMON_OBJ) $(VIDEO_OBJ)
	@echo "Building video process..."
	@$(LD) $(LDFLAGS) -o video_capture.elf $^

ai: $(COMMON_OBJ) $(AI_OBJ)
	@echo "Building AI process..."
	@$(LD) $(LDFLAGS) -o ai_detect.elf $^

manager: $(COMMON_OBJ) $(MANAGER_OBJ)
	@echo "Building manager process..."
	@$(LD) $(LDFLAGS) -o sys_manager.elf $^

# 清理
clean:
	rm -f $(ALL_OBJ) $(PROJECT_NAME) *.elf
	@echo "Clean complete"

# 部署到 SD 卡
deploy: all
	@echo "Deploying to SD card..."
	@mkdir -p /sdcard/fall_detection
	@cp $(PROJECT_NAME) /sdcard/fall_detection/
	@cp model/*.kmodel /sdcard/fall_detection/model/ 2>/dev/null || true
	@cp common/config.json /sdcard/fall_detection/ 2>/dev/null || true
	@echo "Deploy complete"

# 调试
debug: all
	@echo "Starting GDB server..."
	@riscv64-unknown-elf-gdb $(PROJECT_NAME) \
		-ex "target remote :3333" \
		-ex "monitor halt" \
		-ex "load"

# 依赖
-include $(ALL_OBJ:.o=.d)
