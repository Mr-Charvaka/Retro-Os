#include "../drivers/serial.h"
#include "../include/elf.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "../kernel/heap.h" // Added for kfree
#include "../kernel/memory.h"
#include "../kernel/pmm.h"
#include "../kernel/vm.h"

// ... imports ...

uint32_t load_elf(const char *filename, uint32_t *top_address) {
  serial_log("ELF: Loading process...");
  serial_log(filename);

  vfs_node_t *node = vfs_resolve_path(filename);
  if (!node) {
    serial_log("ELF ERROR: Path resolution failed.");
    return 0;
  }

  Elf32_Ehdr ehdr;
  if (vfs_read(node, 0, &ehdr, sizeof(Elf32_Ehdr)) < sizeof(Elf32_Ehdr)) {
    serial_log("ELF ERROR: Header read failed.");
    return 0;
  }

  if (memcmp(ehdr.e_ident, "\x7f\x45\x4c\x46", 4) != 0) {
    serial_log("ELF ERROR: Invalid architecture signature.");
    return 0;
  }

  serial_log_hex("ELF: V-Entry: ", ehdr.e_entry);
  uint32_t max_addr = 0;

  // Load Program Headers locally
  Elf32_Phdr *phdrs = (Elf32_Phdr *)kmalloc(ehdr.e_phnum * sizeof(Elf32_Phdr));
  vfs_read(node, ehdr.e_phoff, phdrs, ehdr.e_phnum * sizeof(Elf32_Phdr));

  for (int i = 0; i < ehdr.e_phnum; i++) {
    if (phdrs[i].p_type == PT_LOAD) {
      uint32_t start_addr = phdrs[i].p_vaddr;
      uint32_t size = phdrs[i].p_memsz;
      uint32_t file_size = phdrs[i].p_filesz;
      
      if (start_addr + size > max_addr) max_addr = start_addr + size;

      uint32_t start_page = (start_addr) & 0xFFFFF000;
      uint32_t end_page = (start_addr + size + 0xFFF) & 0xFFFFF000;

      // Map segments to User space with correct permissions
      for (uint32_t page = start_page; page < end_page; page += 4096) {
        if (vm_get_phys(page) == 0) {
          void *phys = pmm_alloc_block();
          vm_map_page((uint32_t)phys, page, 7); // User/RW (Flags 7)
          memset((void*)page, 0, 4096); // Zero initialised
        }
      }

      // Read segment data directly from disk into its virtual address
      if (file_size > 0) {
        vfs_read(node, phdrs[i].p_offset, (void*)start_addr, file_size);
      }

      // POSIX/ELF requirement: Zero out the rest of the memory in this segment (BSS)
      if (size > file_size) {
        memset((void*)(start_addr + file_size), 0, size - file_size);
        serial_log_hex("ELF: Zeroed BSS for segment, size: ", size - file_size);
      }
    }
  }

  if (top_address) *top_address = (max_addr + 0xFFF) & 0xFFFFF000;
  
  kfree(phdrs);
  return ehdr.e_entry;
}
