import struct
import os

IMG_FILE = "HDD.img"
DISK_SIZE_MB = 51200
SECTOR_SIZE = 512
TOTAL_SECTORS = (DISK_SIZE_MB * 1024 * 1024) // SECTOR_SIZE
RESERVED_SECTORS = 4096
NUM_FATS = 2
SECTORS_PER_FAT = 32768
ROOT_CLUSTER = 2

# Offsets
FAT_OFFSET = RESERVED_SECTORS * SECTOR_SIZE
DATA_OFFSET = (RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT) * SECTOR_SIZE

def create_hdd():
    # 0. Check if it already exists
    if os.path.exists(IMG_FILE):
        print(f"INFO: {IMG_FILE} already exists. Skipping creation to preserve data.")
        return

    # 1. Create empty 32MB file
    print(f"Creating {IMG_FILE} (32MB)...")
    with open(IMG_FILE, "wb") as f:
        f.seek((TOTAL_SECTORS * SECTOR_SIZE) - 1)
        f.write(b'\x00')
        
    # 2. Open and Format as FAT32
    with open(IMG_FILE, "r+b") as f:
        # Write FAT32 BPB
        fmt = "<3s8sHBHBHHBHHIIIIHHIHH12sBBBI11s8s"
        bpb = struct.pack(fmt,
            b'\xEB\x3C\x90', # JMP
            b'MSWIN4.1',     # OEM
            SECTOR_SIZE,     # BytesPerSec
            1,               # SecPerClust
            RESERVED_SECTORS,# ReservedSectors
            NUM_FATS,        # Fats
            0,               # RootEnt
            0,               # TotSec16
            0xF8,            # Media
            0,               # FatSz16
            63,              # SecPerTrk
            16,              # Heads
            0,               # Hidden
            TOTAL_SECTORS,   # TotSec32
            SECTORS_PER_FAT, # FatSz32
            0,               # ExtFlags
            0,               # FSVer
            ROOT_CLUSTER,    # RootClus
            1,               # FSInfo
            6,               # BkBoot
            b'\x00'*12,      # Reserved
            0x80,            # Drv
            0,               # Res
            0x29,            # Sig
            0x99999999,      # VolID (Unique for D: drive)
            b'RETRO-USER ',  # VolLab
            b'FAT32   '      # SysType
        )
        f.seek(0)
        f.write(bpb.ljust(510, b'\x00') + b'\x55\xAA')
        
        # 3. Initialize FAT: Clusters 0, 1 (Reserved) and 2 (Root)
        f.seek(FAT_OFFSET)
        # Cluster 0: Media + FFFFFFF (Lower bits 0xF8)
        # Cluster 1: EOF
        f.write(struct.pack('<III', 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF))
        
        # 4. Clear Root Directory Cluster (Sector 0 of Data area)
        f.seek(DATA_OFFSET)
        f.write(b'\x00' * SECTOR_SIZE)

    print(f"SUCCESS: Created and Formatted {IMG_FILE} for persistent workspace.")

if __name__ == "__main__":
    create_hdd()
