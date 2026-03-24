#include "../include/mbr.h"
#include "ata.h"
#include "../include/string.h"
#include "serial.h"

extern "C" {

int mbr_enumerate_partitions(uint8_t drive_id, partition_info_t *list, int max_count) {
    uint8_t buffer[512];
    if (!ata_read_sector_ext(drive_id, 0, buffer)) {
        serial_log("MBR ERROR: Could not read Sector 0");
        return -1;
    }

    mbr_t *mbr = (mbr_t *)buffer;
    if (mbr->signature != 0xAA55) {
        serial_log("MBR ALERT: Signature mismatch (Assuming Raw/Superfloppy)");
        return 0; // 0 partitions found
    }

    int count = 0;
    for (int i = 0; i < 4; i++) {
        mbr_partition_t *p = &mbr->partitions[i];
        if (p->partition_type == PART_TYPE_EMPTY) continue;
        if (count >= max_count) break;

        list[count].disk_id = drive_id;
        list[count].part_id = i + 1;
        list[count].start_lba = p->start_lba;
        list[count].sector_count = p->sector_count;
        list[count].type = p->partition_type;
        list[count].active = (p->boot_indicator & 0x80) != 0;

        serial_log("MBR: Partition #");
        serial_log_hex("", i + 1);
        serial_log(" Type: ");
        serial_log_hex("", p->partition_type);
        serial_log(" Start: ");
        serial_log_hex("", p->start_lba);

        count++;

        // --- EXTENDED PARTITION SUPPORT (EBR) ---
        if (p->partition_type == PART_TYPE_EXTENDED || p->partition_type == PART_TYPE_EXTENDED_LBA) {
            uint32_t ebr_lba = p->start_lba;
            while (count < max_count) {
                uint8_t ebr_buf[512];
                if (!ata_read_sector_ext(drive_id, ebr_lba, ebr_buf)) break;
                
                mbr_t *ebr = (mbr_t *)ebr_buf;
                if (ebr->signature != 0xAA55) break;

                // First entry in EBR partition table is the actual logical partition
                if (ebr->partitions[0].partition_type != PART_TYPE_EMPTY) {
                   list[count].disk_id = drive_id;
                   list[count].part_id = count + 1;
                   list[count].start_lba = ebr_lba + ebr->partitions[0].start_lba;
                   list[count].sector_count = ebr->partitions[0].sector_count;
                   list[count].type = ebr->partitions[0].partition_type;
                   list[count].active = false;
                   count++;
                }

                // Second entry is the NEXT EBR index (relative to the FIRST Extended Partition Start)
                if (ebr->partitions[1].partition_type == PART_TYPE_EXTENDED || ebr->partitions[1].partition_type == PART_TYPE_EXTENDED_LBA) {
                    ebr_lba = p->start_lba + ebr->partitions[1].start_lba;
                } else {
                    break;
                }
            }
        }
    }
    
    return count;
}

uint32_t mbr_find_first_of_type(uint8_t drive_id, uint8_t type) {
    partition_info_t list[MAX_PARTITIONS_PER_DISK];
    int n = mbr_enumerate_partitions(drive_id, list, MAX_PARTITIONS_PER_DISK);
    for (int i = 0; i < n; i++) {
        if (list[i].type == type) return list[i].start_lba;
    }
    
    // Superfloppy Check
    if (type == PART_TYPE_FAT32 || type == PART_TYPE_FAT32_LBA) {
        uint8_t buf[512];
        if (ata_read_sector_ext(drive_id, 0, buf)) {
            if (memcmp(buf + 82, "FAT32   ", 8) == 0 || memcmp(buf + 54, "FAT32   ", 8) == 0) return 0;
        }
    }
    
    return 0xFFFFFFFF; // Not found
}

}
