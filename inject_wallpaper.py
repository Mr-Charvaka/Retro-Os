import struct
import os

# ============================================================
# Disk Geometry - Must match fat32_bpb_t in fat32.h exactly
# ============================================================
SECTOR_SIZE        = 512
SECTORS_PER_CLUSTER = 32
NUM_FATS           = 2
SECTORS_PER_FAT    = 32768
RESERVED_SECTORS   = 4096   # Must match build.sh dd seek=1 + kernel zone
PARTITION_START    = 2048   # LBA where partition begins (1MB offset)
DISK_SIZE_MB       = 51200
TOTAL_SECTORS      = (DISK_SIZE_MB * 1024 * 1024) // SECTOR_SIZE
ROOT_CLUSTER       = 2
IMG_FILE           = "HDD.img"

# Derived offsets - everything is relative to PARTITION_START
# FAT1 starts at: partition_start + reserved_sectors
FAT1_START_SECTOR  = PARTITION_START + RESERVED_SECTORS
FAT2_START_SECTOR  = FAT1_START_SECTOR + SECTORS_PER_FAT
DATA_START_SECTOR  = FAT2_START_SECTOR + SECTORS_PER_FAT

# Byte offsets into the image file
FAT1_OFFSET        = FAT1_START_SECTOR * SECTOR_SIZE
FAT2_OFFSET        = FAT2_START_SECTOR * SECTOR_SIZE
DATA_OFFSET        = DATA_START_SECTOR * SECTOR_SIZE

# Root directory is at cluster 2 which is the first data cluster
ROOT_DIR_OFFSET    = DATA_OFFSET  # cluster 2 = data start + (2-2)*cluster_size

CLUSTER_SIZE       = SECTORS_PER_CLUSTER * SECTOR_SIZE  # 4096 bytes

def create_fat32_bpb():
    """
    Build a FAT32 BPB that matches fat32_bpb_t in fat32.h exactly.
    Field order and sizes must match the C struct byte for byte.
    """
    fmt = (
        "<"
        "3s"   # jmp
        "8s"   # oem
        "H"    # bytes_per_sector
        "B"    # sectors_per_cluster
        "H"    # reserved_sectors
        "B"    # fats_count
        "H"    # root_entries_count
        "H"    # total_sectors_16
        "B"    # media_type
        "H"    # sectors_per_fat_16
        "H"    # sectors_per_track
        "H"    # heads_count
        "I"    # hidden_sectors      (uint32 - THE KEY FIX)
        "I"    # total_sectors_32
        "I"    # sectors_per_fat_32
        "H"    # ext_flags
        "H"    # fs_version
        "I"    # root_cluster
        "H"    # fs_info_sector
        "H"    # backup_boot_sector
        "12s"  # reserved[12]
        "B"    # drive_num
        "B"    # reserved1
        "B"    # boot_signature
        "I"    # volume_id
        "11s"  # volume_label
        "8s"   # fs_type
    )

    bpb_data = struct.pack(
        fmt,
        b'\xEB\x3C\x90',    # jmp
        b'MSWIN4.1',         # oem
        SECTOR_SIZE,         # bytes_per_sector
        SECTORS_PER_CLUSTER, # sectors_per_cluster
        RESERVED_SECTORS,    # reserved_sectors
        NUM_FATS,            # fats_count
        0,                   # root_entries_count (0 for FAT32)
        0,                   # total_sectors_16   (0 for FAT32)
        0xF8,                # media_type
        0,                   # sectors_per_fat_16 (0 for FAT32)
        63,                  # sectors_per_track
        16,                  # heads_count
        PARTITION_START,     # hidden_sectors = sectors before partition
        TOTAL_SECTORS - PARTITION_START,  # total_sectors_32 (sectors in partition)
        SECTORS_PER_FAT,     # sectors_per_fat_32
        0,                   # ext_flags
        0,                   # fs_version
        ROOT_CLUSTER,        # root_cluster = 2
        1,                   # fs_info_sector (sector 1 of partition)
        6,                   # backup_boot_sector
        b'\x00' * 12,        # reserved
        0x80,                # drive_num
        0,                   # reserved1
        0x29,                # boot_signature
        0x12345678,          # volume_id
        b'RETRO-OS   ',      # volume_label (11 bytes)
        b'FAT32   '          # fs_type (8 bytes)
    )

    # Verify size is exactly 90 bytes (standard BPB size)
    assert len(bpb_data) == 90, f"BPB wrong size: {len(bpb_data)} (expected 90)"

    # Pad to 510 bytes and add boot signature
    result = bpb_data.ljust(510, b'\x00') + b'\x55\xAA'
    assert len(result) == 512
    return result


def create_fsinfo():
    """
    Build FSInfo sector matching fat32_fsinfo_t in fat32.h exactly.
    """
    fmt = (
        "<"
        "I"     # lead_sig
        "480s"  # reserved
        "I"     # struct_sig
        "I"     # free_count
        "I"     # next_free    (THIS WAS MISSING IN OLD CODE)
        "12s"   # reserved2
        "I"     # trail_sig
    )

    fsinfo_data = struct.pack(
        fmt,
        0x41615252,    # lead_sig
        b'\x00' * 480, # reserved
        0x61417272,    # struct_sig
        0xFFFFFFFF,    # free_count (unknown)
        0xFFFFFFFF,    # next_free  (unknown)
        b'\x00' * 12,  # reserved2
        0xAA550000     # trail_sig
    )

    assert len(fsinfo_data) == 512, f"FSInfo wrong size: {len(fsinfo_data)}"
    return fsinfo_data


def write_fat_entry(f, fat_offset_bytes, cluster, value):
    """Write a single FAT entry to a specific FAT (by byte offset)."""
    f.seek(fat_offset_bytes + cluster * 4)
    f.write(struct.pack('<I', value & 0x0FFFFFFF))


def write_fat_entry_both(f, cluster, value):
    """Write a FAT entry to both FAT1 and FAT2 (mirrors)."""
    write_fat_entry(f, FAT1_OFFSET, cluster, value)
    write_fat_entry(f, FAT2_OFFSET, cluster, value)


def inject_file(f, filename_83, source_path, start_cluster):
    """
    Inject a file into the FAT32 image.
    Returns (clusters_used, next_free_cluster).
    """
    if not os.path.exists(source_path):
        print(f"  SKIP: {source_path} not found.")
        return 0, start_cluster

    with open(source_path, "rb") as src:
        data = src.read()

    size = len(data)
    if size == 0:
        print(f"  SKIP: {source_path} is empty.")
        # Create 0-byte file support can be added if needed
        return 0, start_cluster

    clusters_needed = (size + CLUSTER_SIZE - 1) // CLUSTER_SIZE

    # ----------------------------------------------------------------
    # 1. Write file data into data area
    # ----------------------------------------------------------------
    data_write_offset = DATA_OFFSET + (start_cluster - 2) * CLUSTER_SIZE
    f.seek(data_write_offset)
    # Pad data to cluster boundary
    padded = data + b'\x00' * (clusters_needed * CLUSTER_SIZE - size)
    f.write(padded)

    # ----------------------------------------------------------------
    # 2. Write FAT chain (both FAT1 and FAT2)
    # ----------------------------------------------------------------
    for i in range(clusters_needed - 1):
        write_fat_entry_both(f, start_cluster + i, start_cluster + i + 1)
    # Mark last cluster as end of chain
    write_fat_entry_both(f, start_cluster + clusters_needed - 1, 0x0FFFFFFF)

    # ----------------------------------------------------------------
    # 3. Write directory entry into root directory cluster
    # ----------------------------------------------------------------
    # Parse 8.3 filename
    dot_pos = filename_83.find('.')
    if dot_pos >= 0:
        name_part = filename_83[:dot_pos].upper()
        ext_part  = filename_83[dot_pos+1:].upper()
    else:
        name_part = filename_83.upper()
        ext_part  = ""

    name_padded = name_part[:8].ljust(8).encode('ascii')
    ext_padded  = ext_part[:3].ljust(3).encode('ascii')

    cluster_high = (start_cluster >> 16) & 0xFFFF
    cluster_low  = start_cluster & 0xFFFF

    # Directory entry format matching fat32_entry_t in fat32.h
    entry = struct.pack(
        '<8s3sBBBHHHHHHHI',
        name_padded,   # filename[8]
        ext_padded,    # ext[3]
        0x20,          # attributes = ATTR_ARCHIVE
        0,             # reserved
        0,             # create_time_tenth
        0,             # create_time
        0,             # create_date
        0,             # last_access_date
        cluster_high,  # first_cluster_high
        0,             # write_time
        0,             # write_date
        cluster_low,   # first_cluster_low
        size           # file_size
    )
    assert len(entry) == 32, f"Directory entry wrong size: {len(entry)}"

    # Find first empty slot in root directory
    root_entries = ROOT_DIR_OFFSET
    f.seek(root_entries)
    slot_found = False
    for slot in range(CLUSTER_SIZE // 32):
        f.seek(root_entries + slot * 32)
        first_byte = f.read(1)
        if not first_byte or first_byte[0] == 0x00 or first_byte[0] == 0xE5:
            # Empty or deleted slot - write here
            f.seek(root_entries + slot * 32)
            f.write(entry)
            slot_found = True
            break

    if not slot_found:
        print(f"  ERROR: Root directory full! Could not inject {filename_83}")
        return 0, start_cluster

    next_cluster = start_cluster + clusters_needed
    print(f"  OK: {filename_83} ({size} bytes, {clusters_needed} clusters, "
          f"cluster {start_cluster}-{start_cluster + clusters_needed - 1})")
    return clusters_needed, next_cluster


def inject():
    if not os.path.exists(IMG_FILE):
        print(f"ERROR: {IMG_FILE} not found. Run build.sh first.")
        return

    print(f"Opening {IMG_FILE}...")
    print(f"  Partition start : sector {PARTITION_START} "
          f"(byte offset {PARTITION_START * SECTOR_SIZE})")
    print(f"  FAT1 start      : sector {FAT1_START_SECTOR} "
          f"(byte offset {FAT1_OFFSET})")
    print(f"  FAT2 start      : sector {FAT2_START_SECTOR} "
          f"(byte offset {FAT2_OFFSET})")
    print(f"  Data start      : sector {DATA_START_SECTOR} "
          f"(byte offset {DATA_OFFSET})")
    print(f"  Root dir offset : byte {ROOT_DIR_OFFSET}")

    with open(IMG_FILE, "r+b") as f:

        # ------------------------------------------------------------
        # Step 1: Write BPB at partition start
        # ------------------------------------------------------------
        print("\nWriting BPB...")
        f.seek(PARTITION_START * SECTOR_SIZE)
        f.write(create_fat32_bpb())

        # ------------------------------------------------------------
        # Step 2: Write FSInfo at partition_start + 1
        # ------------------------------------------------------------
        print("Writing FSInfo...")
        f.seek((PARTITION_START + 1) * SECTOR_SIZE)
        f.write(create_fsinfo())

        # ------------------------------------------------------------
        # Step 3: Clear both FATs completely
        # ------------------------------------------------------------
        print("Clearing FATs...")
        fat_size = SECTORS_PER_FAT * SECTOR_SIZE
        f.seek(FAT1_OFFSET)
        f.write(b'\x00' * fat_size)
        f.seek(FAT2_OFFSET)
        f.write(b'\x00' * fat_size)

        # ------------------------------------------------------------
        # Step 4: Initialize reserved FAT entries (clusters 0, 1, 2)
        # ------------------------------------------------------------
        print("Initializing FAT reserved entries...")
        write_fat_entry_both(f, 0, 0x0FFFFFF8)
        write_fat_entry_both(f, 1, 0xFFFFFFFF)
        write_fat_entry_both(f, 2, 0x0FFFFFFF)

        # ------------------------------------------------------------
        # Step 5: Clear root directory cluster FULLY
        # ------------------------------------------------------------
        print("Clearing root directory cluster...")
        f.seek(ROOT_DIR_OFFSET)
        f.write(b'\x00' * CLUSTER_SIZE) 

        # ------------------------------------------------------------
        # Step 6: Inject files starting from cluster 3
        # ------------------------------------------------------------
        print("\nInjecting files...")
        current_cluster = 3

        files_to_inject = [
            ("WALL.BMP",  "assets/wallpaper.bmp"),
            ("INIT.ELF",  "apps/init.elf"),
            ("HELLO.ELF", "apps/hello.elf"),
            ("DF.ELF",    "apps/df.elf"),
            ("SH.ELF",    "apps/sh.elf"),
            ("TERM.ELF",  "apps/terminal.elf"),
            ("LS.ELF",    "apps/ls.elf"),
            ("CAT.ELF",   "apps/cat.elf"),
            ("MKDIR.ELF", "apps/mkdir.elf"),
            ("TEXTVIEW.ELF", "apps/textview.elf"),
            ("UTILS.ELF", "apps/file_utils.elf"),
            ("TEST.ELF",  "apps/test.elf"),
            ("PING.ELF",  "apps/ping.elf"),
            ("TCPTEST.ELF", "apps/tcptest.elf"),
            ("WAVPLAY.ELF", "apps/wavplay.elf"),
            ("CHAOS.ELF", "apps/chaos.elf"),
            ("AUDIO.ELF", "apps/audio_demo.elf"),
            ("NETTEST.ELF", "apps/net_test.elf"),
            ("BROWSER.ELF", "apps/netsurf_demo.elf"),
            ("SAMPLE.WAV", "assets/sample.wav"),
            ("TRUTH.DAT",  "TRUTH.DAT"),
        ]

        for name83, path in files_to_inject:
            clusters_used, current_cluster = inject_file(
                f, name83, path, current_cluster
            )

    print(f"\nDone. Last cluster used: {current_cluster - 1}")
    print(f"Files injected into {IMG_FILE}")


if __name__ == "__main__":
    inject()
