#ifndef TELOS_NO_HEAP_H
#define TELOS_NO_HEAP_H

#include <stdlib.h>

/*
 * 无堆路径纪律（No-Heap Path Discipline）
 *
 * 关键路径（每 tick 执行的事件循环、时钟、ID 分配、事件序列
 * 落盘、跟踪缓冲）禁止堆分配：堆分配在嵌入式 / Zephyr 无堆
 * 环境不可用，且分配失败没有恢复路径。
 *
 * 使用方式：
 *   1. 在无堆源文件顶部 #include <telos/no_heap.h>
 *   2. 构建时给该文件加 -DTELOS_NO_HEAP=1（meson 门禁自动处理）
 *   3. 任何 malloc / calloc / realloc / free 调用在编译期报错
 *
 * 注意：宏替换在 include 之后生效，因此本头文件必须位于所有
 * 系统头文件之后（通常就是源文件最后一个 include）。
 */

#ifdef TELOS_NO_HEAP

#if defined(__GNUC__) || defined(__clang__)
#define TELOS_NO_HEAP_ATTR \
    __attribute__((error("heap allocation forbidden in no-heap path")))
#else
#define TELOS_NO_HEAP_ATTR
#endif

void *telos_heap_forbidden(void) TELOS_NO_HEAP_ATTR;

#define malloc(size) telos_heap_forbidden()
#define calloc(count, size) telos_heap_forbidden()
#define realloc(ptr, size) telos_heap_forbidden()
#define free(ptr) telos_heap_forbidden()

#undef TELOS_NO_HEAP_ATTR

#endif /* TELOS_NO_HEAP */

#endif /* TELOS_NO_HEAP_H */
