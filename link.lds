/* K230 RT-Smart 链接脚本 */
/* 根据实际内存布局调整 */

ENTRY(_start)

MEMORY
{
    /* K230 DDR 内存布局 (128MB) */
    RAM (rwx)  : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(RAM);

    /* 代码段 */
    .text : {
        _text_start = .;
        *(.text.start)
        *(.text .text.*)
        _text_end = .;
    } > RAM

    /* 只读数据段 */
    .rodata : {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    } > RAM

    /* 数据段 */
    .data : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > RAM

    /* BSS 段 */
    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /* 堆 (向上增长) */
    . = ALIGN(8);
    _heap_start = .;
    . += 1M;
    _heap_end = .;

    /* 栈 (向下增长) */
    . = ALIGN(8);
    _stack_top = .;
    . += 256K;
    _stack_bottom = .;

    /* 共享内存区域 (IPC 用) */
    . = ALIGN(4096);
    _shm_start = .;
    . += 4M;
    _shm_end = .;

    /* KPU 模型加载区域 */
    . = ALIGN(4096);
    _kpu_mem_start = .;
    . += 2M;
    _kpu_mem_end = .;

    /* 符号导出 */
    PROVIDE(_text_size = _text_end - _text_start);
    PROVIDE(_data_size = _data_end - _data_start);
    PROVIDE(_bss_size = _bss_end - _bss_start);
    PROVIDE(_heap_size = _heap_end - _heap_start);
    PROVIDE(_stack_size = _stack_top - _stack_bottom);
    PROVIDE(_shm_size = _shm_end - _shm_start);
    PROVIDE(_kpu_mem_size = _kpu_mem_end - _kpu_mem_start);
}
