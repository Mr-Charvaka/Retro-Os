import struct
import os

# Disk Geometry (Must match boot.asm)
SECTOR_SIZE = 512
RESERVED_SECTORS = 4096
NUM_FATS = 2
SECTORS_PER_FAT = 256
ROOT_ENTRIES = 512
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32) // SECTOR_SIZE

# Offsets
FAT_OFFSET = RESERVED_SECTORS * SECTOR_SIZE
ROOT_OFFSET = (RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT) * SECTOR_SIZE
DATA_OFFSET = ROOT_OFFSET + (ROOT_DIR_SECTORS * SECTOR_SIZE)

IMG_FILE = "os.img"

def inject_file(f, filename_83, source_path, start_cluster):
    if not os.path.exists(source_path):
        print(f"Error: {source_path} not found.")
        return 0, 0
        
    with open(source_path, "rb") as src:
        data = src.read()
        
    size = len(data)
    sectors = (size + SECTOR_SIZE - 1) // SECTOR_SIZE
    
    # 1. Write Data
    f.seek(DATA_OFFSET + (start_cluster - 2) * SECTOR_SIZE)
    f.write(data)
    
    # 2. Update FAT Table
    f.seek(FAT_OFFSET + start_cluster * 2) 
    cluster = start_cluster
    for i in range(sectors - 1):
        f.write(struct.pack('<H', cluster + 1))
        cluster += 1
    f.write(struct.pack('<H', 0xFFFF))
    
    # 3. Create Directory Entry
    f.seek(ROOT_OFFSET)
    while True:
        entry_data = f.read(32)
        if not entry_data or entry_data[0] == 0:
            if not entry_data: f.seek(0, 2)
            else: f.seek(-32, 1)
            break
            
    name_parts = filename_83.split('.')
    name = name_parts[0].ljust(8)
    ext = name_parts[1].ljust(3) if len(name_parts) > 1 else "   "
    full_name = f"{name}{ext}".encode('ascii')
    
    entry = struct.pack('<11sBBBHHHHHHHL', 
        full_name, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, start_cluster, size)
    f.write(entry)
    
    print(f"Injected {filename_83} ({size} bytes) at cluster {start_cluster}.")
    return sectors, cluster + 1

def inject():
    if not os.path.exists(IMG_FILE):
        print(f"Error: {IMG_FILE} not found.")
        return
        
    with open(IMG_FILE, "r+b") as f:
        # Clear Root Dir and FAT
        f.seek(ROOT_OFFSET)
        f.write(b'\x00' * (ROOT_DIR_SECTORS * SECTOR_SIZE))
        f.seek(FAT_OFFSET)
        f.write(b'\xF8\xFF\xFF\xFF')
        f.seek(FAT_OFFSET + SECTORS_PER_FAT * SECTOR_SIZE)
        f.write(b'\xF8\xFF\xFF\xFF')
        
        current_cluster = 2
        
        files_to_inject = [
            ("WALL.BMP", "assets/wallpaper.bmp"),
            ("INIT.ELF", "apps/init.elf"),
            ("HELLO.ELF", "apps/hello.elf"),
            ("CALC.ELF", "apps/calc.elf"),
            ("DF.ELF", "apps/df.elf"),
            ("EXPLORER.ELF", "apps/explorer.elf"),
            ("SH.ELF", "apps/sh.elf"),
            ("TERM.ELF", "apps/terminal.elf"),
            ("LS.ELF", "apps/ls.elf"),
            ("CAT.ELF", "apps/cat.elf"),
            ("MKDIR.ELF", "apps/mkdir.elf"),
            ("TEXTVIEW.ELF", "apps/textview.elf"),
            ("POSIX_T.ELF", "apps/posix_test.elf"),
            ("POSIX_S.ELF", "apps/posix_suite.elf"),
            ("UTILS.ELF", "apps/file_utils.elf"),
            ("NOTEPAD.ELF", "apps/notepad.elf"),
            ("TEST.ELF", "apps/test.elf"),
            ("PING.ELF", "apps/ping.elf"),
            ("TCPTEST.ELF", "apps/tcptest.elf"),
            ("TRUTH.DAT", "TRUTH.DAT"),
        ]
        
        # Ensure TRUTH.DAT exists for injection
        if not os.path.exists("TRUTH.DAT"):
            # Size = 256 * 66 (approx) + 64 * 4096 + 8
            # Using 300KB to be safe and cover alignment
            with open("TRUTH.DAT", "wb") as t:
                t.write(b'\x00' * 300000)
            print("Created dummy TRUTH.DAT for persistence.")

        for name83, path in files_to_inject:
            if os.path.exists(path):
                sec, next_clust = inject_file(f, name83, path, current_cluster)
                current_cluster = next_clust

if __name__ == "__main__":
    inject()
