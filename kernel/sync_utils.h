#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_

static inline void sync_barrier(volatile int *counter, int all) {

  int local;

  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");

  if (local + 1 < all) {
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}
/*
这段代码实现了一个同步屏障（sync_barrier）函数，用于在多线程或多核系统中实现线程之间的同步。

让我们逐行解释：

static inline void sync_barrier(volatile int *counter, int all) {: 这是函数的声明。函数名为 sync_barrier，
接受两个参数：一个是指向一个 volatile int 类型的指针 counter，另一个是一个 int 类型的参数 all，表示需要达到的同步总数。

int local;: 这里定义了一个本地变量 local，用于存储当前线程或核心的计数器的值。

asm volatile("amoadd.w %0, %2, (%1)\n" : "=r"(local) : "r"(counter), "r"(1) : "memory");: 
这行代码是一个内联汇编代码，用于原子地将 counter 指向的内存地址中的值加上1，并将结果存储到本地变量 local 中。
amoadd.w 是一个原子操作指令，用于原子地将寄存器中的值加到内存地址中的值，并返回结果。

if (local + 1 < all) { ... }: 这是一个条件判断，用于检查当前线程或核心的计数器值加1后是否小于 all。
如果小于，则说明还有其他线程或核心没有达到同步点，需要等待。

do { ... } while (local < all);: 这是一个循环，用于等待其他线程或核心达到同步点。
循环内部的汇编代码 asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory"); 
用于从内存地址 counter 处加载值到本地变量 local 中。
这样可以不断检查其他线程或核心的计数器值，直到所有的计数器都达到同步点。

总的来说，这段代码实现了一个简单的同步屏障，用于保证多线程或多核系统中的各个线程或核心在某个点同步执行。
*/
#endif