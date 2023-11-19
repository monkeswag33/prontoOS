#include <stddef.h>
#include <mem/pmm.h>
#include <mem/bitmap.h>
#include <kernel/multiboot.h>
#include <kernel/logging.h>
#include <sys.h>

extern uint64_t p4_table[512];
extern size_t mem_size;
size_t next_available_addr;
uintptr_t hh_base;

const char *mmap_types[] = {
    "Invalid",
    "Available",
    "Reserved",
    "Reclaimable",
    "NVS",
    "Defective"
};

void clean_table(uint64_t *table) {
    for (int i = 0; i < PAGES_PER_TABLE; i++)
        table[i] = 0;
}

void *map_addr(uint64_t physical, uint64_t address, size_t flags) {
    uint16_t pml4_e = PML4_ENTRY(address);
    uint16_t pdpt_e = PDPR_ENTRY(address);
    uint16_t pd_e = PD_ENTRY(address);
    log(Verbose, "PMM", "Mapping virtual address %x to physical address %x", address, physical);
    log(Debug, "PMM", "PML4: %u, PDPT: %u, PD: %u", pml4_e, pdpt_e, pd_e);
    uint8_t mode = 0;

    if (!IS_HIGHER_HALF(address)) {
        mode = MEM_FLAGS_USER_LEVEL;
        flags |= MEM_FLAGS_USER_LEVEL;
    }

    if (!(p4_table[pml4_e] & PRESENT_BIT)) {
        uint64_t *new_table = alloc_frame();
        p4_table[pml4_e] = (uint64_t) new_table | mode | WRITE_BIT | PRESENT_BIT;
        clean_table(new_table);
    }

    uint64_t *p3_table = (uint64_t*)((p4_table[pml4_e] & PAGE_ADDR_MASK) + KERNEL_VIRTUAL_ADDR);
    if (!(p3_table[pdpt_e] & PRESENT_BIT)) {
        uint64_t *new_table = (uint64_t*) alloc_frame();
        p3_table[pdpt_e] = (uint64_t) new_table | mode | WRITE_BIT | PRESENT_BIT;
        clean_table(new_table);
    }

    uint64_t *p2_table = (uint64_t*)((p3_table[pdpt_e] & PAGE_ADDR_MASK) + KERNEL_VIRTUAL_ADDR);
    p2_table[pd_e] = physical | HUGEPAGE_BIT | flags;
    return (void*) address;
}

void init_pmm(uintptr_t addr, uint32_t size, mb_mmap_t *root) {
    uint32_t mmap_num_entries = (root->size - sizeof(mb_mmap_t)) / root->entry_size;
    mb_mmap_entry_t* mmap_entries = (mb_mmap_entry_t*)(root + 1);
    initialize_bitmap(addr + size, mmap_entries, mmap_num_entries);
    for (uint32_t i = 0; i < mmap_num_entries; i++) {
        if (mmap_entries[i].addr < mem_size && mmap_entries[i].type != 1)
            bitmap_rsv_area(mmap_entries[i].addr, mmap_entries[i].len);
    }
    next_available_addr = HIGHER_HALF_OFFSET + mem_size + PAGE_SIZE; // Add one page for padding
}

void pmm_map_physical_memory(void) {
    for (uint64_t addr = 0, virtual_addr = HIGHER_HALF_OFFSET; addr < mem_size; addr += PAGE_SIZE, virtual_addr += PAGE_SIZE)
        map_addr(addr, virtual_addr, PAGE_TABLE_ENTRY);
    log(Info, "PMM", "Mapped physical memory");
}

void *map_range(uintptr_t start_phys, uintptr_t start_virt, size_t num_pages) {
    for (size_t i = 0; i < num_pages; i++)
        map_addr(start_phys + i * PAGE_SIZE, start_virt + i * PAGE_SIZE, PAGE_TABLE_ENTRY);
    return (void*) start_virt;
}