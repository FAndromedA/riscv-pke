#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_

static inline void sync_barrier(volatile int *counter, int all) {

  int local;

  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");
  /*
  amoadd.w : rd = *rs1; *rs1 += rs2;
  atomically load a 32-bit signed data value from the address in rs1, 
  place the value into register rd, 
  apply add the loaded value and the original 32-bit signed value in rs2, 
  then store the result back to the address in rs1.
  */
  if (local + 1 < all) { 
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}

// added in lab2_challenge3
// the spinlock 自旋锁
// when the signal_lock is 1, the process is blocked
// waiting for the other to run the sync_spinlock_unlock
// when the signal_lock is 0, the process goes on
static inline void sync_spinlock_lock(volatile int *signal_lock) {

  int local;

  asm volatile("amoor.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(signal_lock), "r"(1)
               : "memory");
  //amoor : rd = *rs1; *rs1 |= rs2
  if (local == 1) { // the first one to set the lock shouldn't get in
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(signal_lock) : "memory");
    } while (local != 0);
  }
}

static inline void sync_spinlock_unlock(volatile int *signal_lock) {
  int local;
  asm volatile("amoand.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(signal_lock), "r"(0)
               : "memory");
}

#endif