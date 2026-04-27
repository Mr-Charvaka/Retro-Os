#include "block.h"
#include "nvme.h"
#include "ahci.h"
#include "serial.h"
#include "../include/string.h"
#include "../kernel/memory.h"

extern "C" {

static block_device_t g_devices[MAX_BLOCK_DEVICES];
static int g_device_count = 0;
static uint8_t g_boot_device = 0;

// ============================================================================
// REGISTRATION
// ============================================================================

static uint8_t block_register(block_driver_type_t type, uint8_t drive_id,
                               uint64_t total_sectors, uint32_t sector_size,
                               const char *name) {
    if (g_device_count >= MAX_BLOCK_DEVICES) {
        serial_log("BLOCK: Max devices reached!");
        return 0xFF;
    }

    uint8_t id = (uint8_t)g_device_count;
    g_devices[id].type = type;
    g_devices[id].drive_id = drive_id;
    g_devices[id].total_sectors = total_sectors;
    g_devices[id].sector_size = sector_size;
    g_devices[id].name = name;
    g_device_count++;

    serial_log("BLOCK: Registered device:");
    serial_log_hex("  ID:      ", id);
    serial_log("  Name:    ");
    serial_log(name);
    serial_log_hex("  Type:    ", type);
    serial_log_hex("  Sectors: ", (uint32_t)(total_sectors & 0xFFFFFFFF));
    serial_log_hex("  SecSize: ", sector_size);

    return id;
}

// ============================================================================
// INIT
// ============================================================================

void block_init(void) {
    serial_log("BLOCK: Initializing unified storage layer...");
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    g_boot_device = 0;

    // ── Priority 1: NVMe ──
    if (nvme_init()) {
        uint8_t id = block_register(
            BLOCK_DRIVER_NVME,
            1,
            (uint64_t)(nvme_get_capacity_bytes() / nvme_get_lba_size()),
            nvme_get_lba_size(),
            "NVMe SSD"
        );
        if (id != 0xFF) {
            g_boot_device = id;
            serial_log("BLOCK: NVMe selected as primary storage.");
        }
    }

    // ── Priority 2: AHCI ──
    ahci_init();
    for (uint32_t i = 0; i < g_ahci_device_count; i++) {
        if (ahci_is_ready(i)) {
            uint64_t total_secs = ahci_get_total_sectors(i);
            char *name = (char *)kmalloc(32);
            memcpy(name, "AHCI SATA Drive ", 17);
            name[16] = '0' + i;
            name[17] = '\0';

            uint8_t id = block_register(
                BLOCK_DRIVER_AHCI,
                i,
                total_secs,
                512,
                name
            );
            if (id != 0xFF && g_device_count == 1) {
                g_boot_device = id;
            }
        }
    }

    if (g_device_count == 0) {
        serial_log("BLOCK: WARNING — No storage devices found!");
    }

    serial_log("BLOCK: ════════════════════════════════════");
    serial_log_hex("BLOCK: Total devices: ", g_device_count);
    serial_log_hex("BLOCK: Boot device:   ", g_boot_device);
    serial_log("BLOCK: ════════════════════════════════════");
}

// ============================================================================
// READ
// ============================================================================

bool block_read(uint8_t dev_id, uint64_t lba, uint32_t count, void *buffer) {
    if (dev_id >= g_device_count) return false;

    block_device_t *dev = &g_devices[dev_id];
    bool result = false;

    switch (dev->type) {
        case BLOCK_DRIVER_NVME:
            result = nvme_read_sectors(lba, count, buffer);
            break;

        case BLOCK_DRIVER_AHCI:
            result = ahci_read_sectors(dev->drive_id, lba, count, buffer);
            break;

        default:
            serial_log("BLOCK: Unknown driver type!");
            result = false;
            break;
    }
    return result;
}

// ============================================================================
// WRITE
// ============================================================================

bool block_write(uint8_t dev_id, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev_id >= g_device_count) return false;

    block_device_t *dev = &g_devices[dev_id];
    bool result = false;

    switch (dev->type) {
        case BLOCK_DRIVER_NVME:
            result = nvme_write_sectors(lba, count, buffer);
            break;

        case BLOCK_DRIVER_AHCI:
            result = ahci_write_sectors(dev->drive_id, lba, count, buffer);
            break;

        default:
            result = false;
            break;
    }
    return result;
}

// ============================================================================
// QUERIES
// ============================================================================

block_device_t *block_get_device(uint8_t dev_id) {
    if (dev_id >= g_device_count) return nullptr;
    return &g_devices[dev_id];
}

int block_get_device_count(void) {
    return g_device_count;
}

uint8_t block_get_boot_device(void) {
    return g_boot_device;
}

void block_print_devices(void) {
    serial_log("BLOCK: ═══════ Detected Storage ═══════");
    for (int i = 0; i < g_device_count; i++) {
        serial_log_hex("  [", i);
        serial_log("] ");
        serial_log(g_devices[i].name);

        const char *type_str = "Unknown";
        switch (g_devices[i].type) {
            case BLOCK_DRIVER_AHCI:    type_str = "AHCI-DMA"; break;
            case BLOCK_DRIVER_NVME:    type_str = "NVMe";     break;
            default: break;
        }
        serial_log("    Driver: ");
        serial_log(type_str);
        serial_log_hex("    Sector Size: ", g_devices[i].sector_size);
        serial_log_hex("    Total Sectors: ", (uint32_t)g_devices[i].total_sectors);

        if (i == g_boot_device) {
            serial_log("    ★ BOOT DEVICE");
        }
    }
    serial_log("BLOCK: ════════════════════════════════");
}

} // extern "C"
