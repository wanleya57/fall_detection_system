#!/bin/bash
# ============================================
# 一键 编译 + 运行 脚本 (WSL Linux)
# ============================================

set -e

echo "============================================"
echo "  Fall Detection - PC Simulation"
echo "============================================"
echo ""

# 1. 编译
echo "[1/2] Compiling..."
cd /mnt/c/Users/wmx/Claude.worksapce/pc_sim
make clean 2>/dev/null || true
make
echo "  Build complete!"

# 2. 运行
echo ""
echo "[2/2] Running simulation..."
echo "  - Press ENTER to simulate fall event"
echo "  - Press Q to quit"
echo ""
./fall_detection_pc
