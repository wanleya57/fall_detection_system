# SCons 构建脚本
import os

# 项目根目录
cwd = os.path.dirname(os.path.abspath(__file__))

# 公共模块
src_common = Glob('common/src/*.c')

# 视频进程
src_video = Glob('process_video/src/*.c') + ['process_video/main.c']

# AI 进程
src_ai = Glob('process_ai/src/*.c') + ['process_ai/main.c']

# 管理进程
src_manager = Glob('process_manager/src/*.c') + ['process_manager/main.c']

# 头文件路径
inc_common = os.path.join(cwd, 'common', 'inc')
inc_video = os.path.join(cwd, 'process_video', 'inc')
inc_ai = os.path.join(cwd, 'process_ai', 'inc')
inc_manager = os.path.join(cwd, 'process_manager', 'inc')

# 构建目标
if GetDepend('RT_USING_LWP'):
    # RT-Thread Smart 多进程模式
    objs = []
    objs += SConscript(os.path.join(cwd, 'common', 'SConscript'))
    objs += SConscript(os.path.join(cwd, 'process_video', 'SConscript'))
    objs += SConscript(os.path.join(cwd, 'process_ai', 'SConscript'))
    objs += SConscript(os.path.join(cwd, 'process_manager', 'SConscript'))
else:
    # 单进程模式 (所有模块合并)
    objs = src_common + src_video + src_ai + src_manager

    # 添加头文件路径
    local_inc = [inc_common, inc_video, inc_ai, inc_manager]
    IncludePath(local_inc)

Return('objs')
