#include <drivers/ahci.h>
#include <kernel/logging.h>
#include <mem/kheap.h>
#include <mem/pmm.h>
#include <libc/string.h>
#include <sys.h>
#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	0x96690101

extern void hcf(void);

hba_t *abar;
uintptr_t cmd_list;

static device_type_t get_device_type(hba_port_t *port) {
    if ((port->ssts & 0xF) != 3)
        return NONE;
    if (((port->ssts >> 8) & 0xF) != 1)
        return NONE;
    
    switch (port->sig) {
    case SATA_SIG_ATAPI:
        return ATAPI;
    case SATA_SIG_PM:
        return PM;
    case SATA_SIG_SEMB:
        return SEMB;
    case SATA_SIG_ATA:
        return SATA;
    }

    return NONE;
}

void reset_port(hba_port_t *port, uint8_t port_num) {
    // Stop command engine
    port->cmd &= ~0x01;
    port->cmd &= ~0x10;
    while (port->cmd & 0x4000 || port->cmd & 0x8000);

    // Setup command list buffer
    uintptr_t port_cl = cmd_list + (port_num << 10);
    port->clb = port_cl;
    port->clbu = port_cl >> 32;
    memset((void*) port_cl, 0, 1024);

    // Setup FIS buffer
    uintptr_t port_fis = cmd_list + (32 << 10) + (port_num << 8);
    port->fb = port_fis;
    port->fbu = port_fis >> 32;
    memset((void*) port_fis, 0, 256);

    hba_cmd_header_t *cmd_header = (hba_cmd_header_t*) port_cl;
    for (int i = 0; i < 32; i++) {
        uintptr_t ctba = cmd_list + (32 << 10) + (32 << 8) + (port_num << 13) + (i << 8);
        cmd_header[i].prdtl = 8;
        cmd_header[i].ctba = ctba;
        cmd_header[i].ctbau = ctba >> 32;
        memset((void*) ctba, 0, 256);
    }

    while (port->cmd & 0x8000);
    port->cmd |= 0x10;
    port->cmd |= 0x01;

    log(Verbose, "AHCI", "Reset port %u", port_num);
}

void init_ahci(pci_device_t *ahci) {
    cmd_list = VADDR((uintptr_t) alloc_frame());
    abar = (hba_t*) VADDR(ahci->bars[5].addr);
    map_addr(ALIGN_ADDR(ahci->bars[5].addr), ALIGN_ADDR((uintptr_t) abar), PAGE_TABLE_ENTRY);
    if (ALIGN_ADDR((uintptr_t) abar) != ALIGN_ADDR((uintptr_t) abar + 0x1100))
        map_addr(ALIGN_ADDR_UP(ahci->bars[5].addr), ALIGN_ADDR_UP((uintptr_t) abar), PAGE_TABLE_ENTRY);
    if (!(abar->ghc >> 31)) {
        log(Error, "AHCI", "AHCI isn't enabled");
        hcf();
    }
    for (int i = 0; i < 32; i++) {
        if (!(abar->pi & (1 << i))) continue;
        hba_port_t *port = &abar->ports[i];
        if (!(port->ssts & 0xF)) continue;
        device_type_t type = get_device_type(port);
        log(Info, "AHCI", "Detected device of type %u at port %u", type, i);
        reset_port(port, i);
    }
}
