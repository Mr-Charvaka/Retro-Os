#ifndef AHCI_H
#define AHCI_H

#include "../include/types.h"

// AHCI HBA Register Structure
typedef struct {
    uint32_t clb;       // 0x00, command list base address, 1K-byte aligned
    uint32_t clbu;      // 0x04, command list base address upper 32 bits
    uint32_t fb;        // 0x08, FIS base address, 256-byte aligned
    uint32_t fbu;       // 0x0C, FIS base address upper 32 bits
    uint32_t is;        // 0x10, interrupt status
    uint32_t ie;        // 0x14, interrupt enable
    uint32_t cmd;       // 0x18, command and status
    uint32_t rsv0;      // 0x1C, reserved
    uint32_t tfd;       // 0x20, task file data
    uint32_t sig;       // 0x24, signature
    uint32_t ssts;      // 0x28, SATA status (SCR0:SStatus)
    uint32_t sctl;      // 0x2C, SATA control (SCR2:SControl)
    uint32_t serr;      // 0x30, SATA error (SCR1:SError)
    uint32_t sact;      // 0x34, SATA active (SCR3:SActive)
    uint32_t ci;        // 0x38, command issue
    uint32_t sntf;      // 0x3C, SATA notification (SCR4:SNotification)
    uint32_t fbs;       // 0x40, FIS-based switch control
    uint32_t rsv1[11];  // 0x44-0x6F, reserved
    uint32_t vendor[4]; // 0x70-0x7F, vendor specific
} hba_port_t;

typedef struct {
    uint32_t cap;       // 0x00, Host capability
    uint32_t ghc;       // 0x04, Global host control
    uint32_t is;        // 0x08, Interrupt status
    uint32_t pi;        // 0x0C, Port implemented
    uint32_t vs;        // 0x10, Version
    uint32_t ccc_ctl;   // 0x14, Command completion coalescing control
    uint32_t ccc_pts;   // 0x18, Command completion coalescing ports
    uint32_t em_loc;    // 0x1C, Enclosure management location
    uint32_t em_ctl;    // 0x20, Enclosure management control
    uint32_t cap2;      // 0x24, Host capabilities extended
    uint32_t bohc;      // 0x28, BIOS/OS handoff control and status
    uint8_t  rsv[116];  // 0x2C-0x9F, Reserved
    uint8_t  vendor[96];// 0xA0-0xFF, Vendor specific
    hba_port_t ports[32]; // 0x100-0x10FF, Port control registers
} hba_mem_t;

// Command Header
typedef struct {
    uint8_t  cfl:5;     // FIS length in DWORDs, 2-16
    uint8_t  a:1;       // ATAPI
    uint8_t  w:1;       // Write, 1: H2D, 0: D2H
    uint8_t  p:1;       // Prefetchable
    uint8_t  r:1;       // Reset
    uint8_t  b:1;       // BIST
    uint8_t  c:1;       // Clear busy upon R_OK
    uint8_t  rsv0:1;    // Reserved
    uint8_t  pmp:4;     // Port multiplier port
    uint16_t prdtl;     // PRDT length in entries
    volatile uint32_t prdbc; // PRD byte count transferred
    uint32_t ctba;      // Command table descriptor base address
    uint32_t ctbau;     // Command table descriptor base address upper 32 bits
    uint32_t rsv1[4];   // Reserved
} hba_cmd_header_t;

// PRDT Entry
typedef struct {
    uint32_t dba;       // Data base address
    uint32_t dbau;      // Data base address upper 32 bits
    uint32_t rsv0;      // Reserved
    uint32_t dbc:22;    // Byte count, 4M max
    uint32_t rsv1:9;    // Reserved
    uint32_t i:1;       // Interrupt on completion
} hba_prdt_entry_t;

// Command Table
typedef struct {
    uint8_t  cfis[64];  // Command FIS
    uint8_t  acmd[16];  // ATAPI command, 12 or 16 bytes
    uint8_t  rsv[48];   // Reserved
    hba_prdt_entry_t prdt_entry[1]; // Physical region descriptor table entries, 0 ~ 65535
} hba_cmd_table_t;

// FIS Header
typedef struct {
    uint8_t fis_type;   // FIS_TYPE_REG_H2D
    uint8_t pmport:4;   // Port multiplier
    uint8_t rsv0:3;     // Reserved
    uint8_t c:1;        // 1: Command, 0: Control
    uint8_t command;    // Command register
    uint8_t featurel;   // Feature register, 7:0
    uint8_t lba0;       // LBA low register, 7:0
    uint8_t lba1;       // LBA mid register, 15:8
    uint8_t lba2;       // LBA high register, 23:16
    uint8_t device;     // Device register
    uint8_t lba3;       // LBA register, 31:24
    uint8_t lba4;       // LBA register, 39:32
    uint8_t lba5;       // LBA register, 47:40
    uint8_t featureh;   // Feature register, 15:8
    uint8_t countl;     // Count register, 7:0
    uint8_t counth;     // Count register, 15:8
    uint8_t icc;        // Isochronous command completion
    uint8_t control;    // Control register
    uint8_t rsv1[4];    // Reserved
} fis_reg_h2d_t;

#define SATA_SIG_ATA    0x00000101  // SATA drive
#define SATA_SIG_ATAPI  0xEB140101  // SATAPI drive
#define SATA_SIG_SEMB   0xC33C0101  // Enclosure management bridge
#define SATA_SIG_PM     0x96690101  // Port multiplier

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#ifdef __cplusplus
extern "C" {
#endif

void ahci_init();
bool ahci_read(hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t *buf);
bool ahci_write(hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t *buf);

#ifdef __cplusplus
}
#endif

#endif // AHCI_H
