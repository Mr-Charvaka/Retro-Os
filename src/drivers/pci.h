#ifndef PCI_H
#define PCI_H

#include "../include/types.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                      uint32_t value);
void pci_check_buses();
uint32_t pci_get_bga_bar0(); // Special helper for our goal
bool pci_find_device(uint16_t vendor, uint16_t device, uint8_t *bus,
                     uint8_t *slot, uint8_t *func);

#endif
