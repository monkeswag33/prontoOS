#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t addr;
    bool prefetchable;
    uint8_t mem_type;
    // 1 for I/O Space
    // 0 for Memory Space
    // If I/O Space, mem_type and prefetchable will be 0
    bool io_space;
} pci_bar_t;

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t revision_id;
    uint8_t prog_if;
    pci_bar_t bars[6];
    struct pci_device *next;
} pci_device_t;

void pci_probe(void);
pci_device_t *pci_get_device(uint8_t, uint8_t);
