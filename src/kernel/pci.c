#include <kernel/pci.h>
#include <stdint.h>
#include <kernel/logging.h>
#include <drivers/framebuffer.h>
#include <io/ports.h>
#include <mem/pmm.h>
#include <mem/kheap.h>
#include <sys.h>

pci_device_t *device_list = NULL;

uint32_t pci_get_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
 
    // Create configuration address as per Figure 1
    return (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
}

uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(0xCF8, pci_get_addr(bus, slot, func, offset));
    return inl(0xCFC);
}

uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (pci_read_dword(bus, slot, func, offset) >> ((offset & 2) * 8)) & 0xFFFF;
}

uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (pci_read_word(bus, slot, func, offset) >> ((offset & 1) * 8)) & 0xFF;
}

void pci_write_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t data) {
    uint32_t tmp = pci_read_dword(bus, slot, func, offset); // 0xCF8 already gets set
    tmp &= ~(0xFF << ((offset & 1) * 8));
    tmp |= data << ((offset & 1) * 8);
    outl(0xCFC, tmp);
}

void pci_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t data) {
    pci_write_byte(bus, slot, func, offset, data);
    pci_write_byte(bus, slot, func, offset + 1, data >> 8);
}

void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    pci_write_word(bus, slot, func, offset, data);
    pci_write_word(bus, slot, func, offset + 2, data >> 16);
}

// void pci_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t )

void pci_get_bars(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *device) {
    for (int i = 0; i <= 5; i++) {
        uint32_t offset = 0x10 + i * 4;
        pci_bar_t *device_bar = &device->bars[i];
        uint32_t bar = pci_read_dword(bus, slot, func, offset);
        device_bar->io_space = bar & 1;
        if (bar & 1) {
            device_bar->addr = bar & 0xFFFFFFFC;
            device_bar->prefetchable = 0;
            device_bar->mem_type = 0;
        } else {
            device_bar->mem_type = (bar >> 1) & 0b11;
            device_bar->prefetchable = (bar >> 3) & 1;
            device_bar->addr = bar & 0xFFFFFFF0;
            if (device_bar->mem_type == 2) {
                uint64_t next_part = pci_read_dword(bus, slot, func, 0x10 + ++i * 4);
                device_bar->addr += next_part << 32;
            }
        }
    }
}

void check_device(uint8_t bus, uint8_t slot) {
    uint8_t num_functions = 1;
    uint16_t vendor_id = pci_read_word(bus, slot, 0, 0);
    if (vendor_id == 0xFFFF) return;
    uint8_t header_type = (uint8_t) pci_read_word(bus, slot, 0, 0xE);
    if (header_type & (1 << 7))
        num_functions = 8;
    
    for (int func = 0; func < num_functions; func++) {
        vendor_id = pci_read_word(bus, slot, func, 0);
        if (vendor_id == 0xFFFF) continue;
        pci_device_t *device = kmalloc(sizeof(pci_device_t));
        device->bus = bus;
        device->slot = slot;
        device->function = func;
        device->vendor_id = vendor_id;
        device->device_id = pci_read_word(bus, slot, func, 2);
        device->subclass = pci_read_byte(bus, slot, func, 0xA);
        device->class_code = pci_read_byte(bus, slot, func, 0xB);
        device->revision_id = pci_read_byte(bus, slot, func, 0x8);
        device->prog_if = pci_read_byte(bus, slot, func, 0x9);
        // pci_write_word(bus, slot, func, 0x4, pci_read_word(bus, slot, func, 0x4) | 0b11);
        log(Verbose, "PCI", "(B: %u, D: %u, F: %u) Found device with class %x and subclass %x",
            bus, slot, func, device->class_code, device->subclass, device->prog_if);
        pci_get_bars(bus, slot, func, device);
        device->next = NULL;
        if (device_list == NULL)
            device_list = device;
        else {
            pci_device_t *prev = device_list;
            while (prev->next)
                prev = prev->next;
            prev->next = device;
        }
    }
}

void pci_probe(void) {
    for (int bus = 0; bus < 0xFF; bus++) {
        for (int device = 0; device < 32; device++)
            check_device(bus, device);
    }
    // hcf();
}

pci_device_t *pci_get_device(uint8_t class_code, uint8_t subclass) {
    pci_device_t *device = device_list;
    while (device != NULL) {
        if (device->class_code == class_code && device->subclass == subclass)
            return device;
        device = device->next;
    }
    return NULL;
}
