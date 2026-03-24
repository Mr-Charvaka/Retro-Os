import struct
import os

IMG_FILE = "HDD.img"
SECTOR_SIZE = 512
# Same constants as inject_wallpaper.py
PARTITION_START = 2048
RESERVED_SECTORS = 4096
NUM_FATS = 2
SECTORS_PER_FAT = 32768
DATA_START_SECTOR = PARTITION_START + RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT)
ROOT_DIR_OFFSET = DATA_START_SECTOR * SECTOR_SIZE

def read_root_dir():
    if not os.path.exists(IMG_FILE):
        print(f"Error: {IMG_FILE} not found.")
        return

    with open(IMG_FILE, "rb") as f:
        f.seek(ROOT_DIR_OFFSET)
        # Read the first cluster of root dir
        cluster_data = f.read(32768) # 32 sectors * 512
        
        print(f"{'Filename':<12} | {'Size':<10} | {'Cluster':<10}")
        print("-" * 40)
        
        for i in range(0, len(cluster_data), 32):
            entry = cluster_data[i:i+32]
            if not entry or entry[0] == 0x00:
                break
            if entry[0] == 0xE5:
                continue
                
            name = entry[0:8].decode('ascii', errors='ignore').strip()
            ext = entry[8:11].decode('ascii', errors='ignore').strip()
            if ext:
                full_name = f"{name}.{ext}"
            else:
                full_name = name
                
            size = struct.unpack("<I", entry[28:32])[0]
            cluster_low = struct.unpack("<H", entry[26:28])[0]
            cluster_high = struct.unpack("<H", entry[20:22])[0]
            cluster = (cluster_high << 16) | cluster_low
            
            print(f"{full_name:<12} | {size:<10} | {cluster:<10}")

if __name__ == "__main__":
    read_root_dir()
