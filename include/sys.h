#pragma once
#define KERNEL_VIRTUAL_ADDR 0xFFFFFFFF80000000
#define HIGHER_HALF_OFFSET 0xFFFF800000000000
#define PAGE_SIZE 0x200000
#define END_MAPPED_MEMORY ((511UL << 39) | (510UL << 30) | (511 << 21))
#define IS_HIGHER_HALF(addr) ((addr) >= HIGHER_HALF_OFFSET)
#define VADDR(addr) ((addr) | HIGHER_HALF_OFFSET)
#define PADDR(addr) ((addr) & ~HIGHER_HALF_OFFSET)
#define ALIGN_ADDR(address) ((address) & ~(PAGE_SIZE - 1))
#define ALIGN_ADDR_UP(address) (((address) + PAGE_SIZE) / (PAGE_SIZE + 1) * PAGE_SIZE)
#define PAGES_PER_TABLE 512
#define PML4_ENTRY(address)(((address)>>39) & 0x1ff)
#define PDPR_ENTRY(address)(((address)>>30) & 0x1ff)
#define PD_ENTRY(address)(((address)>>21) & 0x1ff)
#define CPU_ADDRESSES_ADDR (HIGHER_HALF_OFFSET + PAGE_SIZE)
#define PRESENT_BIT 1
#define WRITE_BIT 0b10
#define HUGEPAGE_BIT 0b10000000
#define UNCACHEABLE 0b10000
#define PAGE_TABLE_ENTRY HUGEPAGE_BIT | WRITE_BIT | PRESENT_BIT | UNCACHEABLE