/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "util/functions.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// current points to the currently running user-mode application.
process* current = NULL;

// points to the first free page in our simple heap. added @lab2_2
uint64 g_ufree_page = USER_FREE_ADDRESS_START;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  // SSTATUS_SPP and SSTATUS_SPIE are defined in kernel/riscv.h
  // set S Previous Privilege mode (the SSTATUS_SPP bit in sstatus register) to User mode.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  // write x back to 'sstatus' register to enable interrupts, and sret destination mode.
  write_csr(sstatus, x);

  // set S Exception Program Counter (sepc register) to the elf entry pc.
  write_csr(sepc, proc->trapframe->epc);

  // make user page table. macro MAKE_SATP is defined in kernel/riscv.h. added @lab2_1
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // return_to_user() is defined in kernel/strap_vector.S. switch to user mode with sret.
  // note, return_to_user takes two parameters @ and after lab2_1.
  return_to_user(proc->trapframe, user_satp);
}

//added in lab2_challenge2
uint64 alloc_in_block(int n) { // n bytes
  n = ROUNDUP(n, 8); // must be a multiple of 8
  // sprint("%lld-->", PGSIZE - sizeof(mBlock));
  // if (n > PGSIZE - sizeof(mBlock)) panic("alloc in block failed cause the size is too large! maxsize is %lld\n", PGSIZE - sizeof(mBlock));
  mBlock *nw = current->freeB_head, *pre = NULL;
  while (nw != NULL) {
    if (nw->size >= n) break;
    pre = nw;
    nw = nw->nextBlock;
  }
  if (nw == NULL) {
    void *pa = alloc_page();
    uint64 va = g_ufree_page;
    mBlock *tmp = (mBlock*)pa;
    tmp->pa_st = (uint64)pa + sizeof(mBlock);
    tmp->va_st = va + sizeof(mBlock);
    tmp->size = PGSIZE - sizeof(mBlock);
    tmp->nextBlock = NULL;
    user_vm_map(current->pagetable, va, PGSIZE, (uint64)pa, prot_to_type(PROT_READ | PROT_WRITE, 1));
    g_ufree_page += PGSIZE;
    while(tmp->size < n) { // alloc more than one page to a block if it's not enough
      pa = alloc_page();
      va = g_ufree_page;
      tmp->size += PGSIZE;
      user_vm_map(current->pagetable, va, PGSIZE, (uint64)pa, prot_to_type(PROT_READ | PROT_WRITE, 1));
      g_ufree_page += PGSIZE;
    }
    if (pre == NULL) {
      current->freeB_head = nw = tmp;
    }
    else if (pre->va_st + pre->size == (pre->va_st/PGSIZE * PGSIZE + PGSIZE)) { 
      // the previous block and the last block can be merged
      nw = pre;
      pre->size += PGSIZE;
    }
    else {
      nw = tmp;
      pre->nextBlock = tmp;
    }
  }
  if (nw->size - n > sizeof(mBlock)) { 
    // the rest of the block are usable
    void *pa = (void*)(nw->pa_st + n);
    mBlock *res = (mBlock*) pa;
    res->pa_st = (uint64)pa + sizeof(mBlock);
    res->va_st = nw->va_st + n + sizeof(mBlock);
    res->size = nw->size - n - sizeof(mBlock);
    res->nextBlock = nw->nextBlock;
    // only allocate the needed part
    nw->size = n;
    if (pre == NULL) current->freeB_head = res;
    else pre->nextBlock = res;
  }
  else {
    // the rest is not usable
    // allocate the whole block
    if (current->freeB_head == nw) {
      current->freeB_head = nw->nextBlock;
    }
    else pre->nextBlock = nw->nextBlock;
  }
  // push it to the busy list
  nw->nextBlock = current->busyB_head;
  current->busyB_head = nw;
  sprint("%lld\n", nw->va_st);
  return nw->va_st;
}

//added in lab2_challenge2
void free_in_block(uint64 va) {
  mBlock *nw = current->busyB_head, *pre = NULL;
  while(nw) {
    if (nw->va_st <= va && nw->va_st + nw->size > va) 
      break;
    pre = nw;
    nw = nw->nextBlock;  
  }
  if (nw == NULL) { // not find in block
    // user_vm_unmap(current->pagetable, va, PGSIZE, 1); // do not consider mutiple pages free
    panic("invalid address to free");
    return;
  }
  // remove it from the list
  if (pre == NULL) {
    current->busyB_head = nw->nextBlock;
  }
  else pre->nextBlock = nw->nextBlock;
  // find the place to insert
  mBlock *f_nw = current->freeB_head, *f_pre = NULL;
  while(f_nw) {
    if (f_nw->va_st > nw->va_st + nw->size) break;
    f_pre = f_nw;
    f_nw = f_nw->nextBlock;
  }
  if (f_pre == NULL) {
    nw->nextBlock = current->freeB_head;
    current->freeB_head = nw;
  }
  else {
    f_pre->nextBlock = nw;
    nw->nextBlock = f_nw;
  }
  // merge block
  if (f_pre != NULL && f_pre->va_st + f_pre->size == nw->va_st - sizeof(mBlock)) {
    //merge with f_pre
    f_pre->size += nw->size + sizeof(mBlock);
    f_pre->nextBlock = f_nw;
    nw = f_pre; // Convenient for the following code processing
  }
  if (f_nw != NULL && nw->va_st + nw->size == f_nw->va_st - sizeof(mBlock)) {
    //merge with f_nw
    nw->size += f_nw->size +sizeof(mBlock);
    nw->nextBlock = f_nw->nextBlock;
  }
}