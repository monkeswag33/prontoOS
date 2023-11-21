#include <fs/ahci.h>
#include <kernel/logging.h>
#include <mem/kheap.h>
#include <mem/pmm.h>
#include <libc/string.h>
#include <sys.h>
#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	0x96690101
#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08
#define HBA_PxIS_TFES   (1 << 30)
#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX     0x35
#define ATA_CMD_IDENTIFY 0xEC

extern void hcf(void);

hba_mem_t *abar;
uintptr_t ahci_space;
disk_info_t disks[32];
uint8_t num_disks;

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
    uintptr_t port_cl = ahci_space + (port_num << 10);
    port->clb = port_cl;
    port->clbu = port_cl >> 32;
    memset((void*) port_cl, 0, 1024);

    // Setup FIS buffer
    uintptr_t port_fis = ahci_space + (32 << 10) + (port_num << 8);
    port->fb = port_fis;
    port->fbu = port_fis >> 32;
    memset((void*) port_fis, 0, 256);

    hba_cmd_header_t *cmd_header = (hba_cmd_header_t*) port_cl;
    for (int i = 0; i < 32; i++) {
        cmd_header[i].prdtl = 8;
        uintptr_t ctba = ahci_space + (40 << 10) + (port_num << 13) + (i << 8);
        cmd_header[i].ctba = ctba;
        cmd_header[i].ctbau = ctba >> 32;
        memset((void*) ctba, 0, 256);
    }

    while (port->cmd & 0x8000);
    port->cmd |= 0x10;
    port->cmd |= 0x01;
}

int get_free_cmdslot(hba_port_t *port) {
    uint32_t slots = port->sact | port->ci;
    uint32_t num_slots = (abar->cap & 0xF00) >> 8;
    for (uint32_t i = 0; i < num_slots; i++) {
        if (!(slots & 1))
            return i;
        slots >>= 1;
    }
    log(Warning, "AHCI", "Cannot find free command slot");
    return -1;
}

bool readwrite(bool write, uint8_t fis_command, hba_port_t *port, uint64_t start_sector, uint32_t count, uintptr_t buf) {
    uint32_t startl = start_sector & 0xFFFFFFFF;
    uint16_t starth = (start_sector >> 32) & 0xFFFF;
    port->is = 0xFFFFFFFF;
    int slot = get_free_cmdslot(port);
    if (slot == -1) return false;

    hba_cmd_header_t *cmd_header = (hba_cmd_header_t*) (port->clb | ((uint64_t) port->clbu << 32));
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = write;
    cmd_header->c = 1;
    cmd_header->p = 1;
    cmd_header->prdtl = (uint16_t) ((count - 1) >> 4) + 1;

    hba_cmd_tbl_t *cmd_tbl = (hba_cmd_tbl_t*) (cmd_header->ctba | ((uint64_t) cmd_header->ctbau << 32));
    int i = 0;
    for (; i < cmd_header->prdtl - 1; i++) {
        cmd_tbl->prdt_entry[i].dba = (uint32_t) (buf & 0xFFFFFFFF);
        cmd_tbl->prdt_entry[i].dbau = (uint32_t) ((buf >> 32) & 0xFFFFFFFF);
        cmd_tbl->prdt_entry[i].dbc = 8 * 1024 - 1;
        cmd_tbl->prdt_entry[i].i = 0;
        buf += 8 * 1024;
        count -= 16;
    }

    cmd_tbl->prdt_entry[i].dba = (uint32_t) (buf & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[i].dbau = (uint32_t) ((buf << 32) & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[i].dbc = (count << 9) - 1;
    cmd_tbl->prdt_entry[i].i = 0;

    fis_reg_h2d_t *cmd_fis = (fis_reg_h2d_t*) &cmd_tbl->cfis;
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1;
    cmd_fis->command = fis_command;

    cmd_fis->lba0 = (uint8_t) startl;
    cmd_fis->lba1 = (uint8_t) (startl >> 8);
    cmd_fis->lba2 = (uint8_t) (startl >> 16);
    cmd_fis->device = 1 << 6;

    cmd_fis->lba3 = (uint8_t) (startl >> 24);
    cmd_fis->lba4 = (uint8_t) starth;
    cmd_fis->lba5 = (uint8_t) (starth >> 8);
    cmd_fis->countl = count & 0xFF;
    cmd_fis->counth = count >> 8;

    while (port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ));
    port->ci = port->ci | (1 << slot);
    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;

        if (port->is & HBA_PxIS_TFES) {
            log(Error, "AHCI", "Read disk error");
            return false;
        }
    }
    if (port->is & HBA_PxIS_TFES) {
        log(Error, "AHCI", "Read disk error");
        return false;
    }

    return true;
}

bool read_data(hba_port_t *port, uint64_t start_sector, uint32_t count, uintptr_t buf) {
    return readwrite(false, ATA_CMD_READ_DMA_EX, port, start_sector, count, buf);
}

bool write_data(hba_port_t *port, uint64_t start_sector, uint32_t count, uintptr_t buf) {
    return readwrite(true, ATA_CMD_WRITE_DMA_EX, port, start_sector, count, buf);
}

void identify_drive(hba_port_t *port, uintptr_t buf) {
    port->is = 0xFFFFFFFF;
    int slot = get_free_cmdslot(port);
    if (slot == -1)
        return;
    hba_cmd_header_t *cmd_header = (hba_cmd_header_t*) (port->clb | ((uintptr_t) port->clbu << 32));
    memset(cmd_header, 0, sizeof(hba_cmd_header_t));
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 0;
    cmd_header->c = 1;
    cmd_header->p = 1;
    cmd_header->prdtl = 1;
    hba_cmd_tbl_t *cmd_tbl = (hba_cmd_tbl_t*) (cmd_header->ctba | ((uintptr_t) cmd_header->ctbau << 32));
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));
    cmd_tbl->prdt_entry[0].dba = (uint32_t) ((uintptr_t) buf & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[0].dbau = (uint32_t) (((uintptr_t) buf << 32) & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[0].dbc = 512 - 1;
    cmd_tbl->prdt_entry[0].i = 1;
    fis_reg_h2d_t *cmd_fis = (fis_reg_h2d_t*) cmd_tbl->cfis;
    memset(cmd_fis, 0, sizeof(fis_reg_h2d_t));
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1;
    cmd_fis->command = ATA_CMD_IDENTIFY;
    cmd_fis->device = 0;
    while (port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ));
    port->ci = port->ci | (1 << slot);
    while ((port->ci & (1 << slot)));
}

void init_ahci(pci_device_t *ahci) {
    ahci_space = (uintptr_t) alloc_frame();
    abar = (hba_mem_t*) VADDR(ahci->bars[5].addr);
    map_addr(ALIGN_ADDR(ahci->bars[5].addr), ALIGN_ADDR((uintptr_t) abar), PAGE_TABLE_ENTRY);
    if (ALIGN_ADDR((uintptr_t) abar) != ALIGN_ADDR((uintptr_t) abar + 0x1100))
        map_addr(ALIGN_ADDR_UP(ahci->bars[5].addr), ALIGN_ADDR_UP((uintptr_t) abar), PAGE_TABLE_ENTRY);
    if (!(abar->ghc >> 31)) {
        log(Error, "AHCI", "AHCI isn't enabled");
        hcf();
    }
    ata_identify_device_t *buf = (ata_identify_device_t*) kmalloc(512);
    for (int i = 0; i < 32; i++) {
        if (!(abar->pi & (1 << i))) continue;
        hba_port_t *port = &abar->ports[i];
        if (!(port->ssts & 0xF)) continue;
        device_type_t type = get_device_type(port);
        log(Info, "AHCI", "Detected device of type %u at port %u", type, i);
        reset_port(port, i);
        identify_drive(&abar->ports[0], (uintptr_t) buf);
        disks[num_disks].port_no = i;
        disks[num_disks].port = &abar->ports[i];
        disks[num_disks].num_sectors = buf->lba48_sectors;
        num_disks++;
        log(Verbose, "AHCI", "Found disk at port %u with %u sectors", i, buf->lba48_sectors);
    }
    kfree(buf);
}
