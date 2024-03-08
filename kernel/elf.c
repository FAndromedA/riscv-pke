/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "vmm.h"
#include "pmm.h"
#include "vfs.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

typedef struct elf_info_t {
  struct file *f;
  process *p;
} elf_info;

//
// the implementation of allocater. allocates memory space for later segment loading.
// this allocater is heavily modified @lab2_1, where we do NOT work in bare mode.
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  elf_info *msg = (elf_info *)ctx->info;
  // we assume that size of proram segment is smaller than a page.
  kassert(size < PGSIZE);
  void *pa = alloc_page();
  if (pa == 0) panic("uvmalloc mem alloc falied\n");

  //memset((void *)pa, 0, PGSIZE); don't know why
  user_vm_map((pagetable_t)msg->p->pagetable, elf_va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

  return pa;
}

//
// actual file reading, using the vfs file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  vfs_lseek(msg->f, offset, SEEK_SET);
  return vfs_read(msg->f, dest, nb);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions.
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info. added @lab3_1
    int j;
    for( j=0; j<PGSIZE/sizeof(mapped_region); j++ ) //seek the last mapped region
      if( (process*)(((elf_info*)(ctx->info))->p)->mapped_info[j].va == 0x0 ) break;

    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].va = ph_addr.vaddr;
    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].npages = 1;

    // SEGMENT_READABLE, SEGMENT_EXECUTABLE, SEGMENT_WRITABLE are defined in kernel/elf.h
    if( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = CODE_SEGMENT;
      sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }else if ( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = DATA_SEGMENT;
      sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }else
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    ((process*)(((elf_info*)(ctx->info))->p))->total_mapped_region ++;
  }

  return EL_OK;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p, char *filename) {
  sprint("Application: %s\n", filename);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = vfs_open(filename, O_RDONLY);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the vfs file
  vfs_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

// added in lab4_challenge3 same as lab4_challenge2
// substitute the segment of process
elf_status elf_substitute(elf_ctx *ctx) {
  // structure of program segment header
  elf_prog_header ph_addr; 
  size_t i; 
  uint64 off;
  process *cur = ((elf_info*)ctx->info)->p;
  // traverse the elf program segment headers
  // said in elf_alloc_mb :
  // we assume that size of proram segment is smaller than a page
  
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; ++ i, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr))
      return EL_EIO;
    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    if (ph_addr.flags == (SEGMENT_READABLE | SEGMENT_EXECUTABLE)) {
      // CODE_SEGMENT
      for(size_t j = 0;j < cur->total_mapped_region;++ j) {
        if (cur->mapped_info[j].seg_type == CODE_SEGMENT) {
          // replace the previous one
          // first unmap then you can map the new one
          // do not free in lab4_challenge3 because you share it with the parent
          user_vm_unmap(cur->pagetable, cur->mapped_info[j].va, PGSIZE, 0);
          void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);
          // already allocate in the elf_alloc_mb function
          //user_vm_map(cur->pagetable, ph_addr.vaddr, PGSIZE, dest, prot_to_type(PROT_EXEC|PROT_READ, 1));
          if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
            return EL_EIO;
          cur->mapped_info[j].va = ph_addr.vaddr;
          cur->mapped_info[j].npages = 1;
          sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j ); 
          break;
        }
      }
    }
    else if (ph_addr.flags == (SEGMENT_READABLE | SEGMENT_WRITABLE)) {
      // DATA_SEGMENT
      bool found = 0;
      for(size_t j = 0;j < cur->total_mapped_region;++ j) {
        if (cur->mapped_info[j].seg_type == DATA_SEGMENT) {
          // replace the previous one
          // first unmap then you can map the new one
          user_vm_unmap(cur->pagetable, cur->mapped_info[j].va, PGSIZE, 1); 
          void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);
          // already allocate in the elf_alloc_mb() function
          //user_vm_map(cur->pagetable, ph_addr.vaddr, PGSIZE, dest, prot_to_type(PROT_WRITE|PROT_READ, 1));
          if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
            return EL_EIO;
          
          cur->mapped_info[j].va = ph_addr.vaddr;
          cur->mapped_info[j].npages = 1;
          sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
          found = 1;
          break;
        }
      }
      // data_segment might not exist before
      if (!found) {
        // not found, then create a new one
        size_t j = ++ cur->total_mapped_region;
        void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);
        if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
            return EL_EIO;
        
        cur->mapped_info[j].va = ph_addr.vaddr;
        cur->mapped_info[j].npages = 1;
        cur->mapped_info[j].seg_type = DATA_SEGMENT;
        sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j);
      }
    }
    else {
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );
    }
  }
  // clear the stack_segment and heap_segment
  for(size_t i = 0;i < cur->total_mapped_region;++ i) {
    if (cur->mapped_info[i].seg_type == STACK_SEGMENT) {
      cur->trapframe->regs.sp = USER_STACK_TOP; // assume only one page
    }
    else if (cur->mapped_info[i].seg_type == HEAP_SEGMENT) {
      for(uint64 j = cur->user_heap.heap_bottom;j < cur->user_heap.heap_top;j += PGSIZE) {
        user_vm_unmap(cur->pagetable, j, PGSIZE, 1);
      }
      cur->mapped_info[i].npages = 0;
      cur->user_heap.free_pages_count = 0;
      cur->mapped_info[i].va = USER_FREE_ADDRESS_START;
      cur->user_heap.heap_top = USER_FREE_ADDRESS_START;
      cur->user_heap.heap_bottom = USER_FREE_ADDRESS_START;
    }
  }
  return EL_OK;
}

// substitute bincode from elf to switch the executale
// @param p current process
// @param path path of virtual file system
int32 switch_executable(process *p, char *path) {
  sprint("Application: %s\n", path);
  elf_ctx elfloader; //sprint("%s\n", path);
  elf_info info;
  struct file *elf_vf = vfs_open(path, O_RDONLY); 
  // because the interface changed ( in lab4_challenge2 they accept spike_file,
  // but in lab4_challenge3 they accept virtual_file
  // if (hostfs_hook_open(elf_vf->f_dentry->dentry_inode, elf_vf->f_dentry) == -1) {
  //   //panic("hostfs_hook_open cannot open the given file.\n");
  //   return -1;
  // }
  info.f = elf_vf;
  info.p = p;
  if (IS_ERR_VALUE(info.f)) {
    // panic("Fail on openning the input application program.\n");
    return -1;
  }
  
  if (elf_init(&elfloader, &info) != EL_OK) {
    sprint("fail to init elfloader.\n");
    return -1;
  }
  if (elf_substitute(&elfloader) != EL_OK) {
    sprint("fail to substitute process's segment");
    return -1;
  }
  // the entry of the new program
  p->trapframe->epc = elfloader.ehdr.entry;
  // close the file
  vfs_close(elf_vf);

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
  return 0;
}