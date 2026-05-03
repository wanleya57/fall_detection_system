#!/usr/bin/env python3
"""
C 代码语法检查器
检查常见语法问题，不依赖编译器
"""
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 需要检查的目录
CHECK_DIRS = [
    'common/inc',
    'common/src',
    'process_video/inc',
    'process_video/src',
    'process_ai/inc',
    'process_ai/src',
    'process_manager/inc',
    'process_manager/src',
    'pc_sim',
]

# 常见问题模式
ISSUE_PATTERNS = [
    (r'ret_t\s', 'ret_t 未定义，应使用 fall_err_t'),
    (r'printf\s*\(', '请使用 LOG_I/LOG_E/LOG_D 代替 printf'),
    (r'malloc\s*\(', '请使用 rt_malloc 代替 malloc'),
    (r'free\s*\(', '请使用 rt_free 代替 free'),
    (r'strcpy\s*\(', '请使用 rt_strncpy 代替 strcpy'),
    (r'strcat\s*\(', '字符串拼接建议使用 rt_snprintf'),
]

# 头文件保护宏检查
HEADER_GUARD_RE = re.compile(r'#ifndef\s+(\w+_H__?)\s*\n#define\s+\1')

def check_file(filepath):
    """检查单个文件"""
    issues = []
    rel_path = os.path.relpath(filepath, PROJECT_ROOT)

    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            lines = content.split('\n')
    except Exception as e:
        issues.append(f"  无法读取: {e}")
        return issues

    # 检查头文件保护宏
    if filepath.endswith('.h'):
        if not HEADER_GUARD_RE.search(content):
            issues.append(f"  缺少头文件保护宏 (#ifndef/#define)")

    # 检查常见问题
    for pattern, msg in ISSUE_PATTERNS:
        for i, line in enumerate(lines, 1):
            # 跳过注释
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('/*'):
                continue
            if re.search(pattern, line):
                # 在 pc_sim 目录下允许使用标准库
                if 'pc_sim' in filepath and ('malloc' in pattern or 'free' in pattern or 'printf' in pattern):
                    continue
                issues.append(f"  L{i}: {msg}")

    # 检查括号匹配
    open_parens = content.count('(') - content.count(')')
    open_braces = content.count('{') - content.count('}')
    if open_parens != 0:
        issues.append(f"  括号不匹配: ( 差 {open_parens} 个")
    if open_braces != 0:
        issues.append(f"  花括号不匹配: {{ 差 {open_braces} 个")

    # 检查分号 (简单检查)
    for i, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue
        if stripped.startswith('#'):
            continue
        if stripped.endswith('{') or stripped.endswith('}') or stripped.endswith(','):
            continue
        if 'typedef' in stripped or 'struct' in stripped or 'enum' in stripped:
            continue
        if not stripped.endswith(';') and not stripped.endswith(':') and not stripped.endswith('\\'):
            if not stripped.endswith(',') and not stripped.endswith('//'):
                pass  # 很多情况不需要分号，跳过

    return issues

def main():
    print("=" * 60)
    print("  Fall Detection - C Code Syntax Check")
    print("=" * 60)
    print()

    total_files = 0
    total_issues = 0
    files_with_issues = 0

    for check_dir in CHECK_DIRS:
        dir_path = os.path.join(PROJECT_ROOT, check_dir)
        if not os.path.exists(dir_path):
            continue

        for root, dirs, files in os.walk(dir_path):
            for fname in files:
                if not fname.endswith(('.c', '.h')):
                    continue

                filepath = os.path.join(root, fname)
                total_files += 1
                issues = check_file(filepath)

                rel_path = os.path.relpath(filepath, PROJECT_ROOT)
                if issues:
                    files_with_issues += 1
                    total_issues += len(issues)
                    print(f"[WARN] {rel_path}")
                    for issue in issues:
                        print(issue)
                    print()
                else:
                    print(f"[OK]   {rel_path}")

    print()
    print("=" * 60)
    print(f"  Total: {total_files} files checked, {files_with_issues} with issues")
    print(f"  Issues: {total_issues}")
    print("=" * 60)

    # 检查关键文件是否存在
    print()
    print("Checking critical files...")
    critical_files = [
        'common/inc/fall_common.h',
        'common/inc/ipc_protocol.h',
        'common/src/ipc_protocol.c',
        'process_video/main.c',
        'process_ai/main.c',
        'process_manager/main.c',
        'pc_sim/rtthread_mock.h',
        'pc_sim/rtthread_mock.c',
        'pc_sim/main_pc.c',
        'pc_sim/Makefile',
    ]

    all_ok = True
    for f in critical_files:
        path = os.path.join(PROJECT_ROOT, f)
        if os.path.exists(path):
            print(f"  [OK] {f}")
        else:
            print(f"  [MISSING] {f}")
            all_ok = False

    print()
    if all_ok:
        print("All critical files present!")
    else:
        print("Some critical files are missing!")

    return 0 if total_issues == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
