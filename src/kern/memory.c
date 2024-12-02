// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
};

struct pte {
    uint64_t flags:8;
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))

// Internal constants defintions
//

#define PGSIZE 3096 // Page size (4 kB)
#define USER_BASE 0x00000000 // Start of user space?
#define USER_TOP 0x80000000 // End of user space?

// INTERNAL FUNCTION DECLARATIONS
//

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
static inline struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static inline void sfence_vma(void);
static inline struct pte * walk_pt(struct pte* root, uintptr_t vma, int create);

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

// EXPORTED FUNCTION DEFINITIONS
// 

void memory_init(void) {
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);
    
    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = round_up_ptr(heap_start, PAGE_SIZE);
    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += round_up_size (
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE;

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt);

    // Put free pages on the free page list
    // TODO: FIXME implement this (must work with your implementation of
    // memory_alloc_page and memory_free_page).

    for (pp = heap_end + PAGE_SIZE; pp < RAM_END; pp += PAGE_SIZE){
        memory_free_page(pp);
    }
    
    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

// Allocates a physical page of memory.
// Returns a pointer to the direct-mapped address of the page.
// Does not fail; panics if there are no free pages available.
void * memory_alloc_page(void) {
    trace("%s()", __func__);

    // Allocate a page from the free list
    if(free_list == NULL){
        panic("Out of physical memory");
    }
    void *pp = free_list->padding;
    free_list = free_list->next;
    // Zero out the allocated page before use
    memset(pp, 0, PAGE_SIZE);
    return pp;
}

// Returns a physical memory page to the physical page allocator.
// The page must have been previously allocated by memory_alloc_page.
void memory_free_page(void * pp) {
    trace("%s(%p)", __func__, pp);

    // Return the page to the free list
    union linked_page * newPage = (union linked_page *)pp;
    newPage->next = free_list;
    free_list = newPage;
}

// Allocates and maps a physical page.
// Maps a virtual page to a physical page in the current memory space.
// The /vma/ argument gives the virtual address of the page to map.
// The /rwxug_flags/ argument is an OR of the PTE flags.
void * memory_alloc_and_map_page(uintptr_t vma, uint_fast8_t rwxug_flags) {
    trace("%s(0x%lx, 0x%x)", __func__, vma, rwxug_flags);

    // Allocate a physical page
    void *pp = memory_alloc_page();
    struct pte page_table_entry = leaf_pte(pp, rwxug_flags);


    uintptr_t pt1_ppn = main_pt2[VPN2(vma)].ppn;
    uint64_t pt1_flags = main_pt2[VPN2(vma)].flags;
    // Check if root PTE refers to gigapage - if so, map it
    if(pt1_flags & (PTE_R | PTE_W | PTE_X) != 0){

    }



    // Map the page into the current page table
    int result = memory_map_page(active_space_root(), vma, pp, rwxug_flags);
    if (result != 0) {
        // Mapping failed; panic
        panic("Failed to map page");
    }

    return (void *)vma;
}

// Allocates and maps multiple physical pages in an address range.
// Equivalent to calling memory_alloc_and_map_page for every page in the range.
void * memory_alloc_and_map_range(uintptr_t vma, size_t size, uint_fast8_t rwxug_flags) {
    trace("%s(0x%lx, %zu, 0x%x)", __func__, vma, size, rwxug_flags);

    uintptr_t addr = vma;
    uintptr_t end = vma + size;

    // Allocate and map each page in the range
    while (addr < end) {
        memory_alloc_and_map_page(addr, rwxug_flags);
        addr += PAGE_SIZE;
    }
    return (void *)vma;
}

// Changes the PTE flags for all pages in a mapped range.
void memory_set_range_flags(const void * vp, size_t size, uint_fast8_t rwxug_flags) {
    trace("%s(%p, %zu, 0x%x)", __func__, vp, size, rwxug_flags);

    uintptr_t addr = (uintptr_t)vp;
    uintptr_t end = addr + size;

    // Update the flags for each page in the range
    while (addr < end) {
        memory_set_page_flags((const void *)addr, rwxug_flags);
        addr += PAGE_SIZE;
    }
}

// Unmaps and frees all pages with the U bit set in the PTE flags.
void memory_unmap_and_free_user(void) {
    trace("%s()", __func__);

    struct pte * root = active_space_root();

    // Unmap and free all user pages
    unmap_user_pages(root);
}

// Checks if a virtual address range is mapped with specified flags.
// Returns 1 if every virtual page in the range is mapped with at least the specified flags.
int memory_validate_vptr_len(const void * vp, size_t len, uint_fast8_t rwxug_flags) {
    trace("%s(%p, %zu, 0x%x)", __func__, vp, len, rwxug_flags);

    uintptr_t addr = (uintptr_t)vp;
    uintptr_t end = addr + len;

    // Check each page in the range
    while (addr < end) {
        struct pte *pte = walk_pt(active_space_root(), addr, 0);
        if (!pte || !(pte->flags & PTE_V) || ((pte->flags & rwxug_flags) != rwxug_flags)) {
            // Validation failed
            return 0;
        }
        addr += PAGE_SIZE;
    }
    // All pages validated successfully
    return 1;
}

// Checks if the virtual pointer points to a mapped range containing a null-terminated string.
// Returns 1 if the string is valid and accessible with the specified flags.
int memory_validate_vstr(const char * vs, uint_fast8_t ug_flags) {
    trace("%s(%p, 0x%x)", __func__, vs, ug_flags);

    uintptr_t addr = (uintptr_t)vs;

    // Iterate over the string
    while (1) {
        struct pte *pte = walk_pt(active_space_root(), addr, 0);
        if (!pte || !(pte->flags & PTE_V) || ((pte->flags & ug_flags) != ug_flags)) {
            // Validation failed
            return 0;
        }
        char c = *(char *)addr;
        if (c == '\0') {
            // Null terminator found; valid string
            return 1;
        }
        addr++;
    }
}

// Called from excp.c to handle a page fault at the specified address.
// Either maps a page containing the faulting address, or calls process_exit().
void memory_handle_page_fault(const void * vptr) {
    trace("%s(%p)", __func__, vptr);

    uintptr_t addr = (uintptr_t)vptr & ~(PAGE_SIZE - 1); // Align to page boundary

    // Check if the address is within user space
    if (addr >= USER_BASE && addr < USER_TOP) {
        // Map a new page with user read/write permissions
        memory_alloc_and_map_page(addr, PTE_U | PTE_R | PTE_W);
    } else {
        // Invalid access; terminate the process
        process_exit();
    }
}

// Switches the active memory space to the main memory space and reclaims the
// memory space that was active on entry. All physical pages mapped by a user
// mapping are reclaimed.
void memory_space_reclaim(void) {
    trace("%s()", __func__);

    // Switch to the main memory space (kernel page table)
    uintptr_t old_mtag = memory_space_switch(main_mtag);
    sfence_vma(); // Flush the TLB

    // Reclaim the previous memory space
    free_user_page_tables(old_mtag);
}

// INTERNAL FUNCTION DEFINITIONS
//

// Sets the flags of the PTE associated with vp. Only works with 4kB pages.
void memory_set_page_flags(const void *vp, uint8_t rwxug_flags) {
    trace("%s(%p, 0x%x)", __func__, vp, rwxug_flags);

    // Retrieve the PTE for the given virtual address
    struct pte *pte = walk_pt(active_space_root(), (uintptr_t)vp, 0);
    if (pte && (pte->flags & PTE_V)) {
        // Update the PTE flags, preserving V, A, and D flags
        pte->flags = (pte->flags & (PTE_V | PTE_A | PTE_D)) | (rwxug_flags & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G));
        // Flush the TLB to ensure changes take effect
        sfence_vma();
    } else {
        // Invalid PTE; panic
        panic("Invalid page table entry");
    }
}

static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}


static inline struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}

static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}

static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}

struct pte* walk_pt(struct pte* root, uintptr_t vma, int create){
    struct pte* pt2 = root;

    uintptr_t pt1_ppn = pt2[VPN2(vma)].ppn;
    uint64_t pt1_flags = pt2[VPN2(vma)].flags;
    // Check if root PTE refers to gigapage - if so, create a new page table for it
    if(pt1_flags & PTE_V == 0){
        memory_handle_page_fault(vma);
    }
    else if(pt1_flags & (PTE_R | PTE_W | PTE_X) != 0){
        if(!create){
            return NULL;
        }
        pt2[VPN2(vma)].flags &= ~(PTE_R | PTE_W | PTE_X);
        void * pt1_pma = memory_alloc_page();
        pt1_ppn = pageptr_to_pagenum(pt1_pma);
        pt2[VPN2(vma)].ppn = pt1_ppn;
    }

    struct pte* pt1 = pagenum_to_pageptr(pt1_ppn);

    uintptr_t pt0_ppn = pt1[VPN1(vma)].ppn;
    uint64_t pt0_flags = pt1[VPN1(vma)].flags;
    // Check if root PTE refers to gigapage - if so, create a new page table for it
    if(pt0_flags & PTE_V == 0){
        memory_handle_page_fault(vma);
    }
    else if(pt0_flags & (PTE_R | PTE_W | PTE_X) != 0){
        if(!create){
            return NULL;
        }
        pt1[VPN1(vma)].flags &= ~(PTE_R | PTE_W | PTE_X);
        void * pt0_pma = memory_alloc_page();
        pt0_ppn = pageptr_to_pagenum(pt0_pma);
        pt1[VPN1(vma)].ppn = pt0_ppn;
    }

    struct pte* pt0 = pagenum_to_pageptr(pt0_ppn);

    uintptr_t ppn = pt0[VPN0(vma)].ppn;
    uintptr_t pma = pagenum_to_pageptr(ppn) + (vma & 0xFFF);

    return pma;
}