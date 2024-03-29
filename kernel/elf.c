/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "spike_interface/spike_utils.h"

typedef struct elf_info_t {
  spike_file_t *f;
  process *p;
} elf_info;

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // directly returns the virtual address as we are in the Bare mode in lab1_x
  return (void *)elf_va;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
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
// load the elf segments to memory regions as we are in Bare mode in lab1
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
  }

  return EL_OK;
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//added in lab1_challenge1
elf_ctx elf_loader;

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
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
  //added in lab1_challenge1
  elf_loader = elfloader;
  //memcpy(&elf_loader, &elfloader, sizeof(elf_ctx));
  // close the host spike file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

//added in lab1_challenge1
void find_functionName(void* ip) {
  elf_header ehdr = elf_loader.ehdr;
  uint16 shstrndx = ehdr.shstrndx;
  uint64 shoff = ehdr.shoff;
  uint16 shentsize = ehdr.shentsize;
  uint16 shnum = ehdr.shnum;

  uint64 shstraddr = shoff + (uint64)shstrndx * shentsize;
  uint64 shstrtab_offset ;//= *((uint64*)(shstraddr + 24));
  uint64 shstrtab_size ;//= *((uint64*)(shstraddr + 32));
  elf_fpread(&elf_loader, &shstrtab_offset, 8, shstraddr + 24);
  elf_fpread(&elf_loader, &shstrtab_size, 8, shstraddr + 32);
  
  char shstrTableBuf[shstrtab_size];
  //char *strTableBuf; //we implement malloc in lab2_2
  // strTableBuf = (char *)malloc(strtab_size * sizeof(char));
  // if (strTableBuf == NULL) {
  //   panic("strTableBuf Memory allocation failed.\n");
  // } 
  elf_fpread(&elf_loader, shstrTableBuf, shstrtab_size, shstrtab_offset);

  uint64 strtab_offset;
  uint64 strtab_size;
  for(uint16 i = 0;i < shnum;++ i) {
    uint64 sectionP = (shoff + (uint64)i * shentsize);
    uint32 sh_name ;//= *((uint32*)sectionP);
    elf_fpread(&elf_loader, &sh_name, 4, sectionP);
    char* sh_name_str = (char*)((uint64)shstrTableBuf + (uint64)sh_name);

    if (strcmp(sh_name_str, ".strtab") == 0) {
      elf_fpread(&elf_loader, &strtab_offset, 8, sectionP + 24);
      elf_fpread(&elf_loader, &strtab_size, 8, sectionP + 32);
    }
  }
  char strTableBuf[strtab_size];
  elf_fpread(&elf_loader, strTableBuf, strtab_size, strtab_offset);

  for(uint16 i = 0;i < shnum;++ i) {
    uint64 sectionP = (shoff + (uint64)i * shentsize);
    uint32 sh_name ;//= *((uint32*)sectionP);
    elf_fpread(&elf_loader, &sh_name, 4, sectionP);
    char* sh_name_str = (char*)((uint64)shstrTableBuf + (uint64)sh_name);
    
    if (strcmp(sh_name_str, ".symtab") == 0) {
      uint64 syboff ;//= *((uint64*)(sectionP + 24));
      uint64 syb_size ;//= *((uint64*)(sectionP + 32));
      uint64 syb_entsize ;//= *((uint64*)(sectionP + 56));
      elf_fpread(&elf_loader, &syboff, 8, sectionP + 24);
      elf_fpread(&elf_loader, &syb_size, 8, sectionP + 32);
      elf_fpread(&elf_loader, &syb_entsize, 8, sectionP + 56);

      for(uint64 j = 0;j * syb_entsize < syb_size;++ j) {
        uint64 syb_entaddr = syboff + j * syb_entsize;
        uint32 st_name ;//= *((uint63*)(syb_entaddr));
        uint64 st_value ;//= *((uint64*)(syb_entaddr + 8));
        uint64 st_size ;//= *((uint64*)(syb_entaddr + 16));
        elf_fpread(&elf_loader, &st_name, 4, syb_entaddr);
        elf_fpread(&elf_loader, &st_value, 8, syb_entaddr + 8);
        elf_fpread(&elf_loader, &st_size, 8, syb_entaddr + 16);
        // sprint("%lld %lld %lld\n", j, syb_entsize, syb_size);
        if ((uint64)ip >= st_value && (uint64)ip < st_value + st_size) {
          sprint("%s\n",(char*)((uint64)strTableBuf + (uint64)st_name));
          //free(strTableBuf);
          //(strtab_offset + (uint64)*((uint32*)syb_entaddr))
        }
      }
      //break;
    }
  }  
  //free(strTableBuf);
}