//           elf.c
//          

#include "elf.h"
#include "io.h"

#define LOAD_LOCATION 0x80100000
#define LOAD_END      0x81000000

#define ET_EXEC   2   /* executable type */
#define EM_RISCV	243	/* RISC-V */

#define	ELFMAG0		0x7f		/* EI_MAG */
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'

#define	ELFCLASS64	2		/* EI_CLASS */

#define ELFDATA2LSB	1		/* e_ident[EI_DATA] */

struct elf64_hdr {
  unsigned char	e_ident[EI_NIDENT];	/* ELF "magic number" */
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;		/* Entry point virtual address */
  uint64_t e_phoff;		/* Program header table file offset */
  uint64_t e_shoff;		/* Section header table file offset */
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t   p_type;
    uint32_t   p_flags;
    uint64_t   p_offset;
    uint64_t   p_vaddr;
    uint64_t   p_paddr;
    uint64_t   p_filesz;
    uint64_t   p_memsz;
    uint64_t   p_align;
};

int elf_load(struct io_intf *io, void (**entryptr)(struct io_intf *io)){
    struct elf64_hdr *hdr;
    long bytes_read = ioread(io, hdr, sizeof(elf64_hdr));
    //Ensure we read the whole header
    if(bytes_read < sizeof(hdr)){
      return -1000 + bytes_read - sizeof(elf64_hdr);
    }

    // Check ELF ident chars
    // Also verify 64 bit arch, little-endian
    if(hdr->e_ident[0] != ELFMAG0 || hdr->e_ident[1] != ELFMAG1 || hdr->e_ident[2] != ELFMAG2 || hdr->e_ident[3] != ELFMAG3){
      //Incorrect magic number!
      return -1;
    }
    else if(hdr->e_ident[4] != ELFCLASS64){
      //Wrong architecture class!
      return -2;
    }
    else if(hdr->e_ident[5] != ELFDATA2LSB){
      //Wrong data encoding!
      return -3;
    }
    else if(hdr->e_ident[7] != 0 && hdr->e_ident[7] != 1){
      //Not for UNIX!
      return -4;
    }
    
    // Check e_type in header, ensure is ET_EXEC (executable)
    // Verify e_machine = EM_RISCV
    if(hdr->e_type != ET_EXEC){
      //Not an executable!
      return -5;
    }
    else if(hdr->e_machine != EM_RISCV){
      //Not for RISC-V processor!
      return -6;
    }
    
    // At this point, the file is valid, and should be loaded
    // Set entryptr to point to entry point
    *entryptr = hdr->e_entry;

    // Loop program headers
    struct elf64_phdr *phdr;
    for(int idx = 0; idx < e_phnum; idx++){
      ioseek(io, idx * e_phentsize + e_phoff)
      long bytes_read = ioread(io, phdr, sizeof(elf64_phdr));
      //Ensure we read the whole header
      if(bytes_read < sizeof(hdr)){
        return -100 + bytes_read - sizeof(elf64_phdr);
      }



    }

    return 0;
}
