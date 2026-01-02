#include "pci.h"
#include "../include/io.h"
// #include "../drivers/vga.h" // For debugging print if needed (but currently
// console is text mode.. wait, we are in graphics mode, so vga_print won't work
// easily)

// Helper to read config
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset) {
  uint32_t address;
  uint32_t lbus = (uint32_t)bus;
  uint32_t lslot = (uint32_t)slot;
  uint32_t lfunc = (uint32_t)func;

  // Create address: Enable Bit (31) | Bus (23-16) | Slot (15-11) | Func (10-8)
  // | Offset (7-2)
  address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                       (offset & 0xFC) | ((uint32_t)0x80000000));

  outl(PCI_CONFIG_ADDRESS, address);

  return inl(PCI_CONFIG_DATA);
}

// Check device
// Vendor ID is offset 0 (low 16 bits)
// Device ID is offset 0 (high 16 bits)

uint32_t pci_get_bga_bar0() {
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
      // Read Vendor (offset 0)
      uint32_t id = pci_read_config((uint8_t)bus, slot, 0, 0);

      // Check for valid device (Vendor 0xFFFF is invalid)
      if ((id & 0xFFFF) != 0xFFFF) {
        // Check if it is BGA (QEMU/Bochs VGA)
        // Vendor ID: 0x1234
        // Device ID: 0x1111
        if ((id & 0xFFFF) == 0x1234 && ((id >> 16) & 0xFFFF) == 0x1111) {
          // Found it!
          // Read BAR0 (Offset 0x10) to get Framebuffer Address
          uint32_t bar0 = pci_read_config((uint8_t)bus, slot, 0, 0x10);
          // Mask out the bottom 4 bits (flags) to get raw address
          return (bar0 & 0xFFFFFFF0);
        }
      }
    }
  }
  return 0; // Not found
}
