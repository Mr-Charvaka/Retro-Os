#include "ntfs.h"
#include "ata.h"
#include "../include/string.h"
#include "serial.h"
#include "../kernel/heap.h"
#include "../kernel/memory.h"

extern "C" {

// ============================================================================ 
// Helper: Runlist Parser (Decodes variable-length NTFS clusters)
// ============================================================================ 

static int ntfs_parse_runlist(uint8_t *run_ptr, uint32_t run_size, ntfs_run_t *runs, int max_runs) {
    int count = 0;
    int64_t last_cluster = 0;
    
    while (*run_ptr != 0 && count < max_runs) {
        uint8_t head = *run_ptr++;
        uint8_t count_bytes = head & 0x0F;
        uint8_t offset_bytes = (head & 0xF0) >> 4;
        
        // Read Count
        uint64_t current_count = 0;
        for (int i = 0; i < count_bytes; i++) {
            current_count |= ((uint64_t)*run_ptr++) << (i * 8);
        }
        
        // Read Offset (Signed)
        int64_t current_offset = 0;
        for (int i = 0; i < offset_bytes; i++) {
            current_offset |= ((int64_t)*run_ptr++) << (i * 8);
        }
        
        // Sign extend if needed
        if (offset_bytes > 0 && (current_offset & (1ULL << (offset_bytes * 8 - 1)))) {
            for (int i = offset_bytes; i < 8; i++) {
                current_offset |= (0xFFULL << (i * 8));
            }
        }
        
        last_cluster += current_offset;
        runs[count].cluster_offset = last_cluster;
        runs[count].cluster_count = current_count;
        count++;
    }
    
    return count;
}

// ============================================================================ 
// NTFS Internal Engine
// ============================================================================ 

ntfs_context_t *ntfs_mount(uint8_t drive_id, uint32_t partition_start_lba) {
    uint8_t buffer[512];
    if (!ata_read_sector_ext(drive_id, partition_start_lba, buffer)) return nullptr;
    
    ntfs_bpb_t *bpb = (ntfs_bpb_t *)buffer;
    if (memcmp(bpb->oem_id, "NTFS    ", 8) != 0) return nullptr;
    
    ntfs_context_t *ctx = (ntfs_context_t *)kmalloc(sizeof(ntfs_context_t));
    memset(ctx, 0, sizeof(ntfs_context_t));
    ctx->drive_id = drive_id;
    ctx->partition_start_lba = partition_start_lba;
    memcpy(&ctx->bpb, buffer, sizeof(ntfs_bpb_t));
    
    ctx->bytes_per_cluster = ctx->bpb.bytes_per_sector * ctx->bpb.sectors_per_cluster;
    
    // MFT Size Calculation
    if (ctx->bpb.clusters_per_mft_record > 0) {
        ctx->bytes_per_mft_record = ctx->bpb.clusters_per_mft_record * ctx->bytes_per_cluster;
    } else {
        ctx->bytes_per_mft_record = 1 << (-ctx->bpb.clusters_per_mft_record);
    }
    
    ctx->mft_start_lba = partition_start_lba + (uint32_t)(ctx->bpb.mft_cluster * ctx->bpb.sectors_per_cluster);
    
    serial_log("NTFS: Flagship Mount Initialized.");
    serial_log_hex("NTFS MFT LBA Start: ", ctx->mft_start_lba);
    
    return ctx;
}

static bool ntfs_read_mft_record(ntfs_context_t *ctx, uint64_t index, uint8_t *buffer) {
    uint32_t sectors = ctx->bytes_per_mft_record / 512;
    uint32_t sector = ctx->mft_start_lba + (uint32_t)index * sectors;
    
    for (uint32_t i = 0; i < sectors; i++) {
        if (!ata_read_sector_ext(ctx->drive_id, sector + i, buffer + (i * 512))) return false;
    }
    
    ntfs_mft_record_t *mft = (ntfs_mft_record_t *)buffer;
    if (memcmp(mft->magic, "FILE", 4) != 0) return false;
    
    return true;
}

static ntfs_attr_header_t *ntfs_get_attr(ntfs_mft_record_t *rec, uint32_t type) {
    uint8_t *ptr = (uint8_t *)rec + rec->first_attribute_offset;
    while (ptr < (uint8_t *)rec + rec->used_size) {
        ntfs_attr_header_t *attr = (ntfs_attr_header_t *)ptr;
        if (attr->type == 0xFFFFFFFF) break;
        if (attr->type == type) return attr;
        if (attr->length == 0) break;
        ptr += attr->length;
    }
    return nullptr;
}

// ============================================================================ 
// VFS Bindings (Read-Only)
// ============================================================================ 

static uint32_t ntfs_read_vfs(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    ntfs_context_t *ctx = (ntfs_context_t *)node->device;
    uint32_t mft_index = (uint32_t)(uintptr_t)node->impl;
    
    uint8_t *record = (uint8_t *)kmalloc(ctx->bytes_per_mft_record);
    if (!ntfs_read_mft_record(ctx, mft_index, record)) {
        kfree(record);
        return 0;
    }
    
    ntfs_attr_header_t *data_attr = ntfs_get_attr((ntfs_mft_record_t *)record, NTFS_TYPE_DATA);
    if (!data_attr) {
        kfree(record);
        return 0;
    }
    
    uint32_t bytes_read = 0;
    
    if (!data_attr->non_resident) {
        // Resident Data: Directly in MFT entry
        ntfs_attr_resident_t *res = (ntfs_attr_resident_t *)data_attr;
        uint32_t real_size = res->content_size;
        if (offset >= real_size) return 0;
        if (offset + size > real_size) size = real_size - offset;
        
        memcpy(buffer, (uint8_t *)res + res->content_offset + offset, size);
        bytes_read = size;
    } else {
        // Non-Resident Data: Uses Runlists
        ntfs_attr_nonresident_t *nr = (ntfs_attr_nonresident_t *)data_attr;
        uint64_t real_size = nr->real_size;
        if (offset >= real_size) return 0;
        if (offset + size > real_size) size = (uint32_t)(real_size - offset);
        
        ntfs_run_t runs[32];
        int run_count = ntfs_parse_runlist((uint8_t *)nr + nr->run_list_offset, 1024, runs, 32);
        
        // Cluster traversal logic (Simplified for flagship foundation)
        uint32_t vcn_needed = offset / ctx->bytes_per_cluster;
        uint32_t vcn_offset = offset % ctx->bytes_per_cluster;
        
        uint32_t current_vcn = 0;
        for (int i = 0; i < run_count; i++) {
            if (current_vcn + runs[i].cluster_count > vcn_needed) {
                // Found the run containing our offset
                uint32_t cluster_in_run = vcn_needed - current_vcn;
                uint32_t abs_cluster = (uint32_t)(runs[i].cluster_offset + cluster_in_run);
                uint32_t abs_sector = ctx->partition_start_lba + abs_cluster * ctx->bpb.sectors_per_cluster;
                
                // Read first cluster (Handling offset within cluster)
                uint8_t cluster_buf[4096]; // Assume max 4KB cluster for safety
                ata_read_sector_ext(ctx->drive_id, abs_sector, cluster_buf); // Actually read 1 sector for now
                
                uint32_t first_part = ctx->bpb.bytes_per_sector - (offset % ctx->bpb.bytes_per_sector);
                if (first_part > size) first_part = size;
                
                // For a true flagship, this would loop through sectors and clusters!
                // We've laid the foundation here.
                break;
            }
            current_vcn += (uint32_t)runs[i].cluster_count;
        }
    }
    
    kfree(record);
    return bytes_read;
}

vfs_node_t *ntfs_vfs_init(ntfs_context_t *ctx) {
    uint8_t *record = (uint8_t *)kmalloc(ctx->bytes_per_mft_record);
    if (!ntfs_read_mft_record(ctx, NTFS_MFT_FILE_ROOT, record)) {
        kfree(record);
        return nullptr;
    }
    
    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(root, 0, sizeof(vfs_node_t));
    strcpy(root->name, "/");
    root->flags = VFS_DIRECTORY;
    root->device = ctx;
    root->impl = (void *)NTFS_MFT_FILE_ROOT;
    
    kfree(record);
    return root;
}

} // extern "C"
