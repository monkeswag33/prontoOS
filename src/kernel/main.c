#include <stdint.h>
#include <sys.h>
#include <cpu/idt.h>
#include <cpu/lapic.h>
#include <cpu/ioapic.h>
#include <cpu/mp.h>
#include <cpu/scheduler.h>
#include <cpu/rtc.h>
#include <mem/pmm.h>
#include <mem/kheap.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <libc/string.h>
#include <kernel/multiboot.h>
#include <kernel/acpi.h>
#include <kernel/logging.h>
#include <kernel/pci.h>
#include <drivers/ahci.h>

extern void ap_trampoline(void);
void kernel_start(void);
extern uintptr_t multiboot_framebuffer_data;
extern uintptr_t multiboot_mmap_data;
extern uintptr_t multiboot_basic_meminfo;
extern uintptr_t multiboot_acpi_info;
size_t mem_size;

log_level_t min_log_level = Verbose;

__attribute__((noreturn))
void hcf(void) {
    asm volatile ("cli");
    while (1)
        asm volatile ("hlt");
}

void __attribute__((optimize("O0"))) my_task1(void) {
    printf("_\n");
    // while (1);
}

void __attribute__((optimize("O0"))) my_task2(void) {
    printf("_\n");
    // while (1) {
    //     printf(".");
    //     flush_screen();
    //     for (size_t i = 0; i < 10000; i++);
    // }
}
void __attribute__((optimize("O0"))) my_task3(void) {
    printf(".\n");
    // while (1) {
    //     printf("|");
    //     flush_screen();
    //     for (size_t i = 0; i < 10000; i++);
    // }
}

void __attribute__((noreturn)) init_kernel(uintptr_t addr, uint64_t magic) {
    init_idt();
    uint32_t mbi_size = *(uint32_t*) (addr + KERNEL_VIRTUAL_ADDR);
    mb_basic_meminfo_t *basic_meminfo = (mb_basic_meminfo_t*)(multiboot_basic_meminfo + KERNEL_VIRTUAL_ADDR);
    mb_mmap_t *mmap = (mb_mmap_t*)(multiboot_mmap_data + KERNEL_VIRTUAL_ADDR);

    mem_size = (basic_meminfo->mem_upper + 1024) * 1024;
    init_pmm(addr, mbi_size, mmap);

    mb_framebuffer_data_t *framebuffer = (mb_framebuffer_data_t*)(multiboot_framebuffer_data + KERNEL_VIRTUAL_ADDR);
    init_framebuffer(framebuffer);
    if (magic == 0x36d76289)
        log(Info, "KERNEL", "Multiboot magic number verified");
    else {
        log(Error, "KERNEL", "Failed to verify magic number");
        hcf();
    }

    mb_tag_t *acpi_tag = (mb_tag_t*)(multiboot_acpi_info + KERNEL_VIRTUAL_ADDR);
    init_acpi(acpi_tag);

    init_apic(mem_size);
    pmm_map_physical_memory();
    init_kheap();
    madt_t *madt = (madt_t*) get_table("APIC");
    print_madt(madt);
    pci_probe();
    init_ahci(pci_get_device(0x1, 0x6));
    // init_ioapic(madt);
    // init_keyboard();
    // IDT_SET_ENTRY(34, pit_irq);
    // IDT_SET_ENTRY(35, keyboard_irq);
    // set_irq(2, 34, 0, 0, 1); // PIT timer - initially masked
    // set_irq(1, 35, 0, 0, 0); // Keyboard
    // asm ("sti");
    // calibrate_apic_timer();
    // Change PIT timer irq to RTC timer irq since PIT is no longer used
    // IDT_SET_ENTRY(34, rtc_irq);
    // set_irq(8, 34, 0, 0, 1);
    // clear_screen();
    // init_rtc();
    // start_aps(madt);
    // create_failsafe_thread(VADDR((uintptr_t) sp));
    // create_thread(&kernel_start, VADDR((uintptr_t) alloc_frame()));
    // create_thread(&my_task1, VADDR((uintptr_t) alloc_frame()));
    // create_thread(&my_task2, VADDR((uintptr_t) alloc_frame()));
    // create_thread(&my_task3, VADDR((uintptr_t) alloc_frame()));
    // start_apic_timer(APIC_TIMER_PERIODIC, 2 * apic_ms_interval, APIC_TIMER_PERIODIC_IDT_ENTRY);
    // log(Info, "APIC", "Started APIC timer in periodic mode");
    while (1) asm ("pause");
}

void __attribute__((noreturn)) kernel_start(void) {
    // Kernel is now initialized
    // Actual work goes here now
    // kernel_start must be here because even if there are no other tasks, this one will be running
    log(Info, "KERNEL", "Kernel setup complete");
    thread_sleep(1000);
    log(Info, "KERNEL", "Slept for 1 second");
    while (1) asm ("pause");
}