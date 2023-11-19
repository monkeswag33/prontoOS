#pragma once
#include <kernel/pci.h>

#pragma pack(1)

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;

    uint8_t rsv[0xA0 - 0x2C];

    uint8_t vendor[0x100 - 0xA0];

    hba_port_t ports[32];
} hba_t;

typedef struct {
    uint8_t cfl:5;
    uint8_t a:1;
    uint8_t w:1;
    uint8_t p:1;

    uint8_t r:1;
    uint8_t b:1;
    uint8_t c:1;
    
    uint8_t rsv0:1;

    uint8_t pmp:4;
    uint16_t prdtl;

    volatile uint32_t prdbc;

    uint32_t ctba;
    uint32_t ctbau;
    
    uint32_t rsv1[4];
} hba_cmd_header_t;

#pragma pack(0)

typedef enum {
    NONE,
    ATAPI,
    SEMB,
    PM,
    SATA
} device_type_t;

void init_ahci(pci_device_t*);