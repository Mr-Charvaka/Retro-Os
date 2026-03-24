#include "ahci.h"
#include "pci.h"
#include "serial.h"
#include "../kernel/heap.h"
#include "../kernel/paging.h"
#include "../include/string.h"

extern "C" {

static hba_mem_t *abar = nullptr;

void ahci_init() {
    uint8_t bus, slot, func;
    if (!pci_find_by_class(0x01, 0x06, 0x01, &bus, &slot, &func)) {
        serial_log("AHCI: No controller found.");
        return;
    }

    serial_log("AHCI: Controller found.");

    uint32_t bar5 = pci_read_config(bus, slot, func, 0x24);
    if (bar5 == 0) {
        serial_log("AHCI: BAR5 is zero.");
        return;
    }

    uint32_t phys_addr = bar5 & 0xFFFFF000;
    uint32_t virt_addr = PHYS_TO_VIRT(0x40000000);
    paging_map(phys_addr, virt_addr, 3);
    
    abar = (hba_mem_t *)virt_addr;
    serial_log_hex("AHCI: HBA at ", virt_addr);

    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            hba_port_t *port = &abar->ports[i];
            uint32_t ssts = port->ssts;
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;

            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                if (port->sig == SATA_SIG_ATA) {
                    serial_log("AHCI: SATA drive found.");
                    uint32_t clp, fisp;
                    void *cl = kmalloc_real(1024, 1, &clp);
                    memset(cl, 0, 1024);
                    port->clb = clp;
                    port->clbu = 0;

                    void *fis = kmalloc_real(256, 1, &fisp);
                    memset(fis, 0, 256);
                    port->fb = fisp;
                    port->fbu = 0;

                    port->cmd |= (1 << 4);
                    port->cmd |= (1 << 0);
                    serial_log("AHCI: Port ready.");
                }
            }
        }
    }
}

static int find_cmd_slot(hba_port_t *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) return i;
        slots >>= 1;
    }
    return -1;
}

bool ahci_read(hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t *buf) {
    port->is = (uint32_t)-1;
    int slot = find_cmd_slot(port);
    if (slot == -1) return false;

    hba_cmd_header_t *cmdhead = (hba_cmd_header_t *)(PHYS_TO_VIRT(port->clb));
    cmdhead += slot;
    cmdhead->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdhead->w = 0;
    cmdhead->prdtl = 1;

    uint32_t ctp;
    hba_cmd_table_t *cmdtbl = (hba_cmd_table_t *)kmalloc_real(sizeof(hba_cmd_table_t), 0, &ctp);
    memset(cmdtbl, 0, sizeof(hba_cmd_table_t));
    cmdhead->ctba = ctp;
    cmdhead->ctbau = 0;

    cmdtbl->prdt_entry[0].dba = VIRT_TO_PHYS((uint32_t)buf);
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = count * 512 - 1;
    cmdtbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)(cmdtbl->cfis);
    fis->fis_type = 0x27;
    fis->c = 1;
    fis->command = 0x25;
    fis->lba0 = (uint8_t)startl;
    fis->lba1 = (uint8_t)(startl >> 8);
    fis->lba2 = (uint8_t)(startl >> 16);
    fis->device = 1 << 6;
    fis->lba3 = (uint8_t)(startl >> 24);
    fis->lba4 = (uint8_t)starth;
    fis->lba5 = (uint8_t)(starth >> 8);
    fis->countl = (uint8_t)count;
    fis->counth = (uint8_t)(count >> 8);

    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return false;

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) return false;
    }

    kfree(cmdtbl);
    return true;
}

bool ahci_write(hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t *buf) {
    port->is = (uint32_t)-1;
    int slot = find_cmd_slot(port);
    if (slot == -1) return false;

    hba_cmd_header_t *cmdhead = (hba_cmd_header_t *)(PHYS_TO_VIRT(port->clb));
    cmdhead += slot;
    cmdhead->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdhead->w = 1;
    cmdhead->prdtl = 1;

    uint32_t ctp;
    hba_cmd_table_t *cmdtbl = (hba_cmd_table_t *)kmalloc_real(sizeof(hba_cmd_table_t), 0, &ctp);
    memset(cmdtbl, 0, sizeof(hba_cmd_table_t));
    cmdhead->ctba = ctp;
    cmdhead->ctbau = 0;

    cmdtbl->prdt_entry[0].dba = VIRT_TO_PHYS((uint32_t)buf);
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = count * 512 - 1;
    cmdtbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)(cmdtbl->cfis);
    fis->fis_type = 0x27;
    fis->c = 1;
    fis->command = 0x35;
    fis->lba0 = (uint8_t)startl;
    fis->lba1 = (uint8_t)(startl >> 8);
    fis->lba2 = (uint8_t)(startl >> 16);
    fis->device = 1 << 6;
    fis->lba3 = (uint8_t)(startl >> 24);
    fis->lba4 = (uint8_t)starth;
    fis->lba5 = (uint8_t)(starth >> 8);
    fis->countl = (uint8_t)count;
    fis->counth = (uint8_t)(count >> 8);

    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return false;

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) return false;
    }

    kfree(cmdtbl);
    return true;
}

} // extern "C"
