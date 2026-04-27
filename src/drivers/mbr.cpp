#include "../include/mbr.h"
#include "../drivers/block.h"    // *** CHANGED: was ata.h ***
#include "../include/string.h"
#include "../drivers/serial.h"

extern "C" {

int mbr_enumerate_partitions(uint8_t drive_id, partition_info_t *list, int max_count) {
    uint8_t buffer[512];
    // *** CHANGED: was ata_read_sector_ext → now block_read ***
    if (!block_read(drive_id, 0, 1, buffer)) {
        serial_log("MBR ERROR: Could not read Sector 0");
        return -1;
    }

    mbr_t *mbr = (mbr_t *)buffer;
    uint8_t sig_low = buffer[510];
    uint8_t sig_high = buffer[511];

    if (sig_low != 0x55 || sig_high != 0xAA) {
        serial_log("MBR ALERT: Signature mismatch!");
        serial_log_hex("", sig_low);
        serial_log_hex("", sig_high);
        return 0;
    }

    serial_log("MBR: Valid DOS Partition Table found.");

    int count = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t offset = 446 + (i * 16);
        uint8_t type = buffer[offset + 4];
        
        serial_log("MBR: Slot ");
        serial_log_hex("", i);
        serial_log(" type detected: ");
        serial_log_hex("", type);

        if (type == 0) continue;
        if (count >= max_count) break;

        uint32_t start_lba = (uint32_t)buffer[offset + 8] | 
                            ((uint32_t)buffer[offset + 9] << 8) | 
                            ((uint32_t)buffer[offset + 10] << 16) | 
                            ((uint32_t)buffer[offset + 11] << 24);

        uint32_t sector_count = (uint32_t)buffer[offset + 12] | 
                               ((uint32_t)buffer[offset + 13] << 8) | 
                               ((uint32_t)buffer[offset + 14] << 16) | 
                               ((uint32_t)buffer[offset + 15] << 24);

        list[count].disk_id = drive_id;
        list[count].part_id = i + 1;
        list[count].start_lba = start_lba;
        list[count].sector_count = sector_count;
        list[count].type = type;
        list[count].active = (buffer[offset] & 0x80) != 0;

        count++;

        // Extended partition support
        if (type == PART_TYPE_EXTENDED || type == PART_TYPE_EXTENDED_LBA) {
            uint32_t ebr_lba = start_lba;
            while (count < max_count) {
                uint8_t ebr_buf[512];
                // *** CHANGED ***
                if (!block_read(drive_id, (uint64_t)ebr_lba, 1, ebr_buf)) break;
                
                if (ebr_buf[510] != 0x55 || ebr_buf[511] != 0xAA) break;

                uint8_t ltype = ebr_buf[446 + 4];
                if (ltype != 0) {
                   list[count].disk_id = drive_id;
                   list[count].part_id = count + 1;
                   
                   uint32_t rel_lba = (uint32_t)ebr_buf[446 + 8] | 
                                     ((uint32_t)ebr_buf[446 + 9] << 8) | 
                                     ((uint32_t)ebr_buf[446 + 10] << 16) | 
                                     ((uint32_t)ebr_buf[446 + 11] << 24);
                   
                   uint32_t s_count = (uint32_t)ebr_buf[446 + 12] | 
                                     ((uint32_t)ebr_buf[446 + 13] << 8) | 
                                     ((uint32_t)ebr_buf[446 + 14] << 16) | 
                                     ((uint32_t)ebr_buf[446 + 15] << 24);

                   list[count].start_lba = ebr_lba + rel_lba;
                   list[count].sector_count = s_count;
                   list[count].type = ltype;
                   list[count].active = false;
                   count++;
                }

                uint8_t next_type = ebr_buf[446 + 16 + 4];
                if (next_type == PART_TYPE_EXTENDED || next_type == PART_TYPE_EXTENDED_LBA) {
                    uint32_t next_off = (uint32_t)ebr_buf[446 + 16 + 8] | 
                                       ((uint32_t)ebr_buf[446 + 16 + 9] << 8) | 
                                       ((uint32_t)ebr_buf[446 + 16 + 10] << 16) | 
                                       ((uint32_t)ebr_buf[446 + 16 + 11] << 24);
                    ebr_lba = start_lba + next_off;
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
    
    // Superfloppy check
    if (type == PART_TYPE_FAT32 || type == PART_TYPE_FAT32_LBA) {
        uint8_t buf[512];
        // *** CHANGED ***
        if (block_read(drive_id, 0, 1, buf)) {
            if (memcmp(buf + 82, "FAT32   ", 8) == 0 || memcmp(buf + 54, "FAT32   ", 8) == 0) return 0;
        }
    }
    
    return 0xFFFFFFFF;
}

}
