#ifndef __STATUS_H__  // 这是标准的"头文件保护宏"，防止重复包含
#define __STATUS_H__

// 包含必要的头文件，如果你的函数原型需要用到某些类型的话
#include "main.h" 

// --- 函数原型声明 ---
// 告诉其他 .c 文件，存在这两个函数，它们长这样
void status_init(void);
void status_task(void);


#endif /* __STATUS_H__ */

