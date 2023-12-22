/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}


// added in lab1_challenge1
#include "elf.h"

// added in lab1_challenge1
void sys_user_getfuncname(int depth) {
  
  void *bp, *ip;
  // asm volatile ("mv %0, s0" : "=r" (bp));// 64bit
  // bp = __builtin_frame_address(0); 
  // if we move this function to user_call we can use this to get bp
  // but we are now in sys_call
  bp = ((void**)(current->trapframe->regs.s0))[-1]; // 因为do_user_call中直接中断了
  //并且没有调用其它函数，所以其ra直接保存在ra寄存器当中，所以 bp - 8就是上一层的bp
  //即为print_backtrace的bp

  for (int i = 0; i < depth;++ i) {
    ip = ((void **)bp)[-1]; // bp-8 对应ra返回地址
    /*const char *function_name =*/ 
    find_functionName(ip);
    // 根据返回地址，即上一层指令的地址，我们可以在elf中找到上一层的函数名
    // 因为第一层是print_backtrce，不需要打印出来。

    bp = ((void **)bp)[-2]; // bp-16保存的上一层的bp
    if (bp == NULL) {
      break;
    }
  }
  return;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  // sprint("%lld\n",a0);
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    
    // added in lab1_challenge1
    case SYS_print_backtrace:
      //return (uint64)
      sys_user_getfuncname(a1);
      return 0;
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
