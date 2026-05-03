# PC 端模拟环境 Makefile
# 用于在 Linux/WSL 上编译运行，验证代码逻辑

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
CFLAGS += -DRT_USING_MOCK
CFLAGS += -I../common/inc
CFLAGS += -I../pc_sim
CFLAGS += -I../process_video/inc
CFLAGS += -I../process_ai/inc
CFLAGS += -I../process_manager/inc

LDFLAGS = -lpthread -lm

# 源文件
MOCK_SRC = rtthread_mock.c

COMMON_SRC = \
    ../common/src/ipc_protocol.c \
    ../common/src/config.c \
    ../common/src/log.c \
    ../common/src/person_log.c \
    ../common/src/pose_overlay.c

VIDEO_SRC = \
    ../process_video/src/video_capture.c \
    ../process_video/src/video_display.c \
    ../process_video/src/video_osd.c \
    ../process_video/src/video_rtsp.c \
    ../process_video/src/video_record.c

AI_SRC = \
    ../process_ai/src/ai_engine.c \
    ../process_ai/src/fall_detect.c \
    ../process_ai/src/fall_alert.c \
    ../process_ai/src/fall_notify.c \
    ../process_ai/src/fall_storage.c

MANAGER_SRC = \
    ../process_manager/src/sys_init.c \
    ../process_manager/src/key_handler.c \
    ../process_manager/src/event_log.c \
    ../process_manager/src/sys_monitor.c

ALL_SRC = $(MOCK_SRC) $(COMMON_SRC) $(VIDEO_SRC) $(AI_SRC) $(MANAGER_SRC) main_pc.c

# 目标
TARGET = fall_detection_pc

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(ALL_SRC)
	@echo "Building PC simulation..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"
	@echo "Run: ./$(TARGET)"

run: $(TARGET)
	@echo "Starting PC simulation..."
	./$(TARGET)

clean:
	rm -f $(TARGET)
	@echo "Clean complete"
