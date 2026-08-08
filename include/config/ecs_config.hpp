#pragma once

// 全局栈内存控制
// 嵌入式/RTOS 环境定义 LCF_MINIMAL_STACK=1 关闭所有栈分配, 将大数组移至堆分配
// 桌面环境默认 0, 保留栈分配
#ifndef LCF_MINIMAL_STACK
#define LCF_MINIMAL_STACK 0
#endif 