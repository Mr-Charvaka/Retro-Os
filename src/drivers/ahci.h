#ifndef AHCI_H
#define AHCI_H

#include "../include/types.h"

typedef struct {
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

typedef struct {
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
    uint8_t  rsv[116];
    uint8_t  vendor[96];
    hba_port_t ports[32];
} hba_mem_t;

typedef struct {
    uint8_t  cfl:5;
    uint8_t  a:1;
    uint8_t  w:1;
    uint8_t  p:1;
    uint8_t  r:1;
    uint8_t  b:1;
    uint8_t  c:1;
    uint8_t  rsv0:1;
    uint8_t  pmp:4;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} hba_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} hba_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt_entry[8];
} hba_cmd_table_t;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:3;
    uint8_t c:1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv1[4];
} fis_reg_h2d_t;

#define SATA_SIG_ATA    0x00000101
#define SATA_SIG_ATAPI  0xEB140101
#define SATA_SIG_SEMB   0xC33C0101
#define SATA_SIG_PM     0x96690101

#define HBA_PORT_IPM_ACTIVE  1
#define HBA_PORT_DET_PRESENT 3

#define HBA_PxCMD_ST   (1 << 0)
#define HBA_PxCMD_FRE  (1 << 4)
#define HBA_PxCMD_FR   (1 << 14)
#define HBA_PxCMD_CR   (1 << 15)

// Max sectors per single DMA command (64KB = 128 sectors)
#define AHCI_MAX_SECTORS_PER_CMD 128

// AHCI MMIO virtual base
#define AHCI_MMIO_VIRT  0xEE000000
#define AHCI_MMIO_PAGES 4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hba_port_t *port;
    hba_cmd_header_t *clb_virt;
    uint32_t clb_phys;
    uint8_t *fb_virt;
    uint32_t fb_phys;
    hba_cmd_table_t *ctbl_virt;
    uint32_t ctbl_phys;
    uint8_t *bounce_virt;
    uint32_t bounce_phys;
    uint32_t bounce_sectors;
    uint64_t total_sectors;
    volatile int lock;
    bool ready;
} ahci_device_t;

#define MAX_AHCI_DEVICES 32
extern ahci_device_t g_ahci_devices[MAX_AHCI_DEVICES];
extern uint32_t g_ahci_device_count;

void ahci_init(void);
bool ahci_is_ready(uint32_t drive_id);
bool ahci_read_sectors(uint32_t drive_id, uint64_t lba, uint32_t count, void *buffer);
bool ahci_write_sectors(uint32_t drive_id, uint64_t lba, uint32_t count, const void *buffer);
hba_port_t *ahci_get_port(uint32_t drive_id);
uint64_t ahci_get_total_sectors(uint32_t drive_id);

#ifdef __cplusplus
}
#endif

#endif // AHCI_H
