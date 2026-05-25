#include "pci.h"
#include "../include/io.h"
#include "serial.h"

// Config read karne ka helper
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset) {
  uint32_t address;
  uint32_t lbus = (uint32_t)bus;
  uint32_t lslot = (uint32_t)slot;
  uint32_t lfunc = (uint32_t)func;

  // Address banao: Enable Bit (31) | Bus (23-16) | Slot (15-11) | Func (10-8) |
  // Offset (7-2)
  address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                       (offset & 0xFC) | ((uint32_t)0x80000000));

  outl(PCI_CONFIG_ADDRESS, address);
  return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                      uint32_t value) {
  uint32_t address;
  uint32_t lbus = (uint32_t)bus;
  uint32_t lslot = (uint32_t)slot;
  uint32_t lfunc = (uint32_t)func;

  address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                       (offset & 0xFC) | ((uint32_t)0x80000000));

  outl(PCI_CONFIG_ADDRESS, address);
  outl(PCI_CONFIG_DATA, value);
}

// Device check karo
// Vendor ID offset 0 pe hai (neeche ke 16 bits)
// Device ID high 16 bits mein hai

uint32_t pci_get_bga_bar0() {
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
      // Vendor read karo (offset 0)
      uint32_t id = pci_read_config((uint8_t)bus, slot, 0, 0);

      // Dekho device valid hai ya nahi (0xFFFF matlab kuch nahi hai wahan)
      if ((id & 0xFFFF) != 0xFFFF) {
        // Dekho BGA (QEMU/Bochs VGA) hai kya?
        // Vendor ID: 0x1234
        // Device ID: 0x1111
        if ((id & 0xFFFF) == 0x1234 && ((id >> 16) & 0xFFFF) == 0x1111) {
          // Mil gaya bhai!
          // BAR0 (Offset 0x10) se Framebuffer Address nikal lo
          uint32_t bar0 = pci_read_config((uint8_t)bus, slot, 0, 0x10);
          // Neeche ke 4 bits (flags) hatao asli address ke liye
          return (bar0 & 0xFFFFFFF0);
        }
      }
    }
  }
  return 0; // Nahi mila be
}

bool pci_find_device(uint16_t vendor, uint16_t device, uint8_t *bus,
                     uint8_t *slot, uint8_t *func) {
  for (uint16_t b = 0; b < 256; b++) {
    for (uint8_t s = 0; s < 32; s++) {
      for (uint8_t f = 0; f < 8; f++) {
        uint32_t id = pci_read_config((uint8_t)b, s, f, 0);
        if ((id & 0xFFFF) == vendor && ((id >> 16) & 0xFFFF) == device) {
          *bus = (uint8_t)b;
          *slot = s;
          *func = f;
          return true;
        }
      }
    }
  }
  return false;
}

bool pci_find_by_class(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if,
                       uint8_t *bus, uint8_t *slot, uint8_t *func) {
  for (uint16_t b = 0; b < 256; b++) {
    for (uint8_t s = 0; s < 32; s++) {
      for (uint8_t f = 0; f < 8; f++) {
        uint32_t id = pci_read_config((uint8_t)b, s, f, 0);
        if ((id & 0xFFFF) != 0xFFFF) {
          uint32_t class_info = pci_read_config((uint8_t)b, s, f, 0x08);
          uint8_t c = (class_info >> 24) & 0xFF;
          uint8_t sc = (class_info >> 16) & 0xFF;
          uint8_t pi = (class_info >> 8) & 0xFF;
          if (c == class_id && sc == subclass_id && pi == prog_if) {
            *bus = (uint8_t)b;
            *slot = s;
            *func = f;
            return true;
          }
        }
      }
    }
  }
  return false;
}
void pci_probe() {
    serial_log("PCI: Starting hardware probe...");
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read_config((uint8_t)bus, slot, func, 0x00);
                if ((id & 0xFFFF) != 0xFFFF) {
                    uint32_t class_info = pci_read_config((uint8_t)bus, slot, func, 0x08);
                    uint8_t class_id = (class_info >> 24) & 0xFF;
                    uint8_t subclass_id = (class_info >> 16) & 0xFF;
                    uint8_t prog_if = (class_info >> 8) & 0xFF;

                    serial_log("PCI: Bus=");
                    serial_log_hex("", bus);
                    serial_log(" Slot=");
                    serial_log_hex("", slot);
                    serial_log(" V=");
                    serial_log_hex("", id & 0xFFFF);
                    serial_log(" D=");
                    serial_log_hex("", (id >> 16) & 0xFFFF);
                    serial_log(" C=");
                    serial_log_hex("", class_id);
                    serial_log(" SC=");
                    serial_log_hex("", subclass_id);
                    serial_log(" PI=");
                    serial_log_hex("", prog_if);
                }
            }
        }
    }
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    if (bar_index > 5) return 0;
    uint32_t bar_offset = 0x10 + (bar_index * 4);
    return pci_read_config(bus, slot, func, bar_offset);
}
