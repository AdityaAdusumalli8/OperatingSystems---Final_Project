//           elf.c
//          

#include "elf.h"
#include "io.h"
#include "console.h"
#include "string.h"
#include "config.h"
#include "memory.h"

#define PAGE_SIZE 4096

#define EI_NIDENT 16

#define PT_LOAD    1  /* program header type */

#define ET_EXEC   2   /* executable type */
#define EM_RISCV	243	/* RISC-V */

#define	ELFMAG0		0x7f		/* EI_MAG */
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'

#define	ELFCLASS64	2		/* EI_CLASS */

#define ELFDATA2LSB	1		/* e_ident[EI_DATA] */

typedef struct elf64_hdr {
  unsigned char	e_ident[EI_NIDENT];	/* ELF "magic number" */
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uintptr_t e_entry;		/* Entry point virtual address */
  uint64_t e_phoff;		/* Program header table file offset */
  uint64_t e_shoff;		/* Section header table file offset */
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct elf64_phdr {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;		/* Segment file offset */
  uintptr_t p_vaddr;		/* Segment virtual address */
  uintptr_t p_paddr;		/* Segment physical address */
  uint64_t   p_filesz;
  uint64_t   p_memsz;
  uint64_t   p_align;
} Elf64_Phdr;


//elf_load
//loads an elf file into user memory to be ran as a program.
//inputs: io - pointer to the io interface to read the .elf from
//        entryptr - double pointer to write the entry point of the program to
//returns: status of elf_load - 0 represents success
int elf_load(struct io_intf *io, void (**entryptr)(void)){
  console_printf("into elf_load\n");
    struct elf64_hdr hdr;
    long bytes_read = ioread(io, &hdr, sizeof(Elf64_Ehdr));
    //Ensure we read the whole header
    console_printf("read %d bytes\n", bytes_read);
    if(bytes_read < sizeof(Elf64_Ehdr)){
      return -1000 + bytes_read - sizeof(Elf64_Ehdr);
    }

    // Check ELF ident chars
    // Also verify 64 bit arch, little-endian
    if(hdr.e_ident[0] != ELFMAG0 || hdr.e_ident[1] != ELFMAG1 || hdr.e_ident[2] != ELFMAG2 || hdr.e_ident[3] != ELFMAG3){
      //Incorrect magic number!
      return -1;
    }
    else if(hdr.e_ident[4] != ELFCLASS64){
      //Wrong architecture class!
      return -2;
    }
    else if(hdr.e_ident[5] != ELFDATA2LSB){
      //Wrong data encoding!
      return -3;
    }
    else if(hdr.e_ident[7] != 0 && hdr.e_ident[7] != 1){
      //Not for UNIX!
      return -4;
    }

    console_printf("2\n");
    
    // Check e_type in header, ensure is ET_EXEC (executable)
    // Verify e_machine = EM_RISCV
    if(hdr.e_type != ET_EXEC){
      //Not an executable!
      return -5;
    }
    else if(hdr.e_machine != EM_RISCV){
      //Not for RISC-V processor!
      return -6;
    }
    
    // At this point, the file is valid, and should be loaded
    // Set entryptr to point to entry point
    *entryptr = (void *)hdr.e_entry;
    console_printf("3, entry pointer to %x\n", hdr.e_entry);

    // Loop program headers
    struct elf64_phdr phdr;
    for(int idx = 0; idx < hdr.e_phnum; idx++){
      // Move io object to point to program header
      ioseek(io, idx * hdr.e_phentsize + hdr.e_phoff);
      // Read program header into phdr
      long bytes_read = ioread(io, &phdr, sizeof(Elf64_Phdr));
      // Ensure we read the whole header
      if(bytes_read < sizeof(Elf64_Phdr)){
        return -100 + bytes_read - sizeof(Elf64_Phdr);
      }
      // Ensure that the data is loadable into memory
      if(phdr.p_type != PT_LOAD){
        console_printf("skipping a not load!\n");
        continue;
      }

      // Bounds checking
      if(phdr.p_vaddr < USER_START_VMA || phdr.p_vaddr + phdr.p_memsz >= USER_END_VMA){
        // Out of bounds error!
        return -8;
      }

      // Move io object to point to the data to copy
      ioseek(io, phdr.p_offset);
      console_printf("4\n");
      // Read data from io object into memory at mem pointer
      char cpy_buf[phdr.p_memsz];
      memset(cpy_buf, 0, phdr.p_memsz);

      long bytes_to_vaddr = ioread(io, cpy_buf, phdr.p_filesz);

      memory_alloc_and_map_range(phdr.p_vaddr, phdr.p_memsz, (PTE_R | PTE_X | PTE_U));

      memcpy((void *)phdr.p_vaddr, cpy_buf, phdr.p_memsz);
      console_printf("5, wrote %d bytes to %x\n", bytes_to_vaddr, phdr.p_vaddr);
    }

    return 0;
}
