#include "vm.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "paging.h"
#include "pmm.h"
#include "pae.h"
#include "apic.h"

extern "C" bool g_pae_active;
extern "C" uint32_t g_pdpt_phys_addr;

// Import PAE prototypes if they aren't in paging.h
extern "C" void pae_map_page(uint32_t virtual_addr, uint64_t physical_addr, uint64_t flags);
extern "C" void pae_unmap_page(uint32_t virtual_addr);
extern "C" uint64_t vm_get_phys_pae(uint32_t virtual_addr);

uint32_t *pd_create() {
  if (g_pae_active) {
    uint32_t pdpt_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)pdpt_phys, 2);
    // Explicitly zero the 32-byte header (not 4KB to save time/precision)
    for(int i=0; i<4; i++) pdpt->entries[i] = 0;

    for (int i = 0; i < 4; i++) {
        uint32_t pd_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        pae_page_directory_t *pd = (pae_page_directory_t *)pae_map_window((uint64_t)pd_phys, 1);
        memset(pd, 0, 4096);
        pdpt->entries[i] = (uint64_t)pd_phys | 1;
        pae_unmap_window(1);
    }

    extern pae_page_directory_t g_page_dirs[4];

    // Copy ALL kernel/system page directories
    // PDPT[0] — Identity map (0-1GB)
    uint32_t pd0_phys = (uint32_t)(pdpt->entries[0] & PAE_ADDR_MASK);
    pae_page_directory_t *pd0_virt = (pae_page_directory_t *)pae_map_window((uint64_t)pd0_phys, 1);
    memcpy(pd0_virt, &g_page_dirs[0], 4096);
    pae_unmap_window(1);

    // PDPT[2] — MMIO region (2-3GB)  *** THIS WAS MISSING ***
    uint32_t pd2_phys = (uint32_t)(pdpt->entries[2] & PAE_ADDR_MASK);
    pae_page_directory_t *pd2_virt = (pae_page_directory_t *)pae_map_window((uint64_t)pd2_phys, 1);
    memcpy(pd2_virt, &g_page_dirs[2], 4096);
    pae_unmap_window(1);

    // PDPT[3] — Kernel half (0xC0000000+)
    uint32_t pd3_phys = (uint32_t)(pdpt->entries[3] & PAE_ADDR_MASK);
    pae_page_directory_t *pd3_virt = (pae_page_directory_t *)pae_map_window((uint64_t)pd3_phys, 1);
    memcpy(pd3_virt, &g_page_dirs[3], 4096);
    pae_unmap_window(1);

    pae_unmap_window(2);
    return (uint32_t *)(uintptr_t)pdpt_phys;
  }
  uint32_t phys_pd = (uint32_t)(uintptr_t)pmm_alloc_block();
  uint32_t *pd = (uint32_t *)PHYS_TO_VIRT(phys_pd);
  memset(pd, 0, 4096);
  for (int i = 0; i < 1024; i++) {
    if (kernel_directory[i] & 1) pd[i] = kernel_directory[i];
  }
  return (uint32_t *)(uintptr_t)phys_pd;
}

uint32_t *pd_clone(uint32_t *source_pdpt_phys) {
  if (g_pae_active) {
     uint32_t new_pdpt_phys = (uint32_t)(uintptr_t)pd_create();
     // We will map/unmap PDPTs as needed to avoid holding too many slots
     
     for (int d = 0; d < 3; d++) {
         uint32_t old_pd_phys, new_pd_phys;
         
         // Phase 1: Access PDPT to get PDs
         {
             pae_pdpt_t *old_pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)(uintptr_t)source_pdpt_phys, 3);
             pae_pdpt_t *new_pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)new_pdpt_phys, 2);
             if (!(old_pdpt->entries[d] & 1)) {
                 pae_unmap_window(2);
                 pae_unmap_window(3);
                 continue;
             }
             old_pd_phys = (uint32_t)(old_pdpt->entries[d] & PAE_ADDR_MASK);
             new_pd_phys = (uint32_t)(new_pdpt->entries[d] & PAE_ADDR_MASK);
             pae_unmap_window(2);
             pae_unmap_window(3);
         }

         // Phase 2: Traverse PD
         pae_page_directory_t *old_pd = (pae_page_directory_t *)pae_map_window((uint64_t)old_pd_phys, 0);
         pae_page_directory_t *new_pd = (pae_page_directory_t *)pae_map_window((uint64_t)new_pd_phys, 1);

         for (int i = 0; i < 512; i++) {
             if (!(old_pd->entries[i] & 1)) continue;
             
             if (old_pd->entries[i] & PAE_FLAG_PAGESIZE) {
                 new_pd->entries[i] = old_pd->entries[i];
                 continue;
             }
             
             // Get PT physical addresses
             uint32_t old_pt_phys = (uint32_t)(old_pd->entries[i] & PAE_ADDR_MASK);
             uint32_t new_pt_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
             if (!new_pt_phys) continue;

             // Temporarily drop PD windows to map PTs
             pae_unmap_window(1);
             pae_unmap_window(0);
             
             pae_page_table_t *old_pt = (pae_page_table_t *)pae_map_window((uint64_t)old_pt_phys, 0);
             pae_page_table_t *new_pt = (pae_page_table_t *)pae_map_window((uint64_t)new_pt_phys, 1);
             
             // Initialize new PT
             for (int j = 0; j < 512; j++) {
                 if (old_pt->entries[j] & 1) {
                     uint32_t old_f_phys = (uint32_t)(old_pt->entries[j] & PAE_ADDR_MASK);
                     uint32_t new_f_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
                     
                     // Drop PTs to copy data in slots 0,1 (or use 2,3)
                     void *src = pae_map_window((uint64_t)old_f_phys, 2);
                     void *dst = pae_map_window((uint64_t)new_f_phys, 3);
                     memcpy(dst, src, 4096);
                     pae_unmap_window(3);
                     pae_unmap_window(2);

                     new_pt->entries[j] = (uint64_t)new_f_phys | (old_pt->entries[j] & 0xFFF);
                 } else {
                     new_pt->entries[j] = 0;
                 }
             }
             
             pae_unmap_window(1);
             pae_unmap_window(0);
             
             // Restore PD windows to link the new PT
             old_pd = (pae_page_directory_t *)pae_map_window((uint64_t)old_pd_phys, 0);
             new_pd = (pae_page_directory_t *)pae_map_window((uint64_t)new_pd_phys, 1);
             new_pd->entries[i] = (uint64_t)new_pt_phys | (old_pd->entries[i] & 0xFFF);
         }
         pae_unmap_window(1);
         pae_unmap_window(0);
     }
     return (uint32_t *)(uintptr_t)new_pdpt_phys;
  }
  uint32_t phys_new_pd = (uint32_t)(uintptr_t)pd_create();
  uint32_t *new_pd = (uint32_t *)PHYS_TO_VIRT(phys_new_pd);
  uint32_t *source_pd = (uint32_t *)PHYS_TO_VIRT((uint32_t)(uintptr_t)source_pdpt_phys);
  for (int i = 0; i < 768; i++) {
    if (!(source_pd[i] & 1)) continue;
    if (source_pd[i] == (uint32_t)(uintptr_t)kernel_directory[i]) continue;
    uint32_t phys_dest_pt = (uint32_t)(uintptr_t)pmm_alloc_block();
    uint32_t *dest_pt = (uint32_t *)PHYS_TO_VIRT(phys_dest_pt);
    uint32_t *src_pt = (uint32_t *)PHYS_TO_VIRT(source_pd[i] & 0xFFFFF000);
    memset(dest_pt, 0, 4096);
    new_pd[i] = phys_dest_pt | (source_pd[i] & 0xFFF);
    for (int j = 0; j < 1024; j++) {
      if (src_pt[j] & 1) {
        uint32_t dest_phys = (uint32_t)(uintptr_t)pmm_alloc_block();
        memcpy((void *)PHYS_TO_VIRT(dest_phys), (void *)PHYS_TO_VIRT(src_pt[j] & 0xFFFFF000), 4096);
        dest_pt[j] = dest_phys | (src_pt[j] & 0xFFF);
      }
    }
  }
  return (uint32_t *)(uintptr_t)phys_new_pd;
}

void pd_destroy(uint32_t *pdpt_phys) {
  if (g_pae_active) {
      pae_pdpt_t *pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)(uintptr_t)pdpt_phys, 2);
      for (int d = 0; d < 3; d++) {
          if (!(pdpt->entries[d] & 1)) continue;
          uint32_t pd_phys = (uint32_t)(pdpt->entries[d] & PAE_ADDR_MASK);
          pae_page_directory_t *pd = (pae_page_directory_t *)pae_map_window((uint64_t)pd_phys, 1);
          for (int i = 0; i < 512; i++) {
              if (!(pd->entries[i] & 1) || (pd->entries[i] & (1ULL << 7))) continue;
              uint32_t pt_phys = (uint32_t)(pd->entries[i] & PAE_ADDR_MASK);
              pae_page_table_t *pt = (pae_page_table_t *)pae_map_window((uint64_t)pt_phys, 0);
              for (int j = 0; j < 512; j++) {
                  if (pt->entries[j] & 1) pmm_free_block((void *)(uintptr_t)(pt->entries[j] & PAE_ADDR_MASK));
              }
              pae_unmap_window(0);
              pmm_free_block((void *)(uintptr_t)pt_phys);
          }
          pae_unmap_window(1);
          pmm_free_block((void *)(uintptr_t)pd_phys);
      }
      pae_unmap_window(2);
      pmm_free_block((void *)(uintptr_t)pdpt_phys);
      return;
  }
  uint32_t *pd = (uint32_t *)PHYS_TO_VIRT((uint32_t)(uintptr_t)pdpt_phys);
  for (int i = 0; i < 1024; i++) {
    if ((pd[i] & 1) && pd[i] != (uint32_t)(uintptr_t)kernel_directory[i]) {
      uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(pd[i] & 0xFFFFF000);
      for (int j = 0; j < 1024; j++) {
        if (pt[j] & 1) pmm_free_block((void *)(uintptr_t)(pt[j] & 0xFFFFF000));
      }
      pmm_free_block((void *)VIRT_TO_PHYS((uintptr_t)pt));
    }
  }
  pmm_free_block((void *)(uintptr_t)pdpt_phys);
}

void pd_switch(uint32_t *pd_phys) {
  uint32_t phys = (uint32_t)(uintptr_t)pd_phys;
  asm volatile("mov %0, %%cr3" ::"r"(phys));
}

void vm_map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
  if (g_pae_active) {
    pae_map_page(virt, (uint64_t)phys, (uint64_t)flags);
    return;
  }
  uint32_t phys_pd;
  asm volatile("mov %%cr3, %0" : "=r"(phys_pd));
  uint32_t *pd = (uint32_t *)PHYS_TO_VIRT(phys_pd);
  uint32_t pd_index = virt >> 22;
  uint32_t pt_index = (virt >> 12) & 0x03FF;
  if (!(pd[pd_index] & 1)) {
    uint32_t phys_pt = (uint32_t)(uintptr_t)pmm_alloc_block();
    memset((uint32_t *)PHYS_TO_VIRT(phys_pt), 0, 4096);
    pd[pd_index] = phys_pt | 7;
  } else if (flags & 4) pd[pd_index] |= 4;
  uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(pd[pd_index] & 0xFFFFF000);
  pt[pt_index] = (phys & 0xFFFFF000) | flags;
  smp_tlb_shootdown(virt);
}

uint32_t vm_get_phys(uint32_t virt) {
  return (uint32_t)vm_get_phys_pae(virt);
}

void vm_unmap_page(uint32_t virt) {
  if (g_pae_active) {
    pae_unmap_page(virt);
    return;
  }
  uint32_t phys_pd;
  asm volatile("mov %%cr3, %0" : "=r"(phys_pd));
  uint32_t *pd = (uint32_t *)PHYS_TO_VIRT(phys_pd);
  uint32_t pd_index = virt >> 22;
  uint32_t pt_index = (virt >> 12) & 0x03FF;
  if (pd[pd_index] & 1) {
    uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(pd[pd_index] & 0xFFFFF000);
    if (pt[pt_index] & 1) {
      pmm_free_block((void *)(uintptr_t)(pt[pt_index] & 0xFFFFF000));
      pt[pt_index] = 0;
    }
  }
  smp_tlb_shootdown(virt);
}

void vm_clear_user_mappings() {
  if (g_pae_active) {
      uint32_t pdpt_phys;
      asm volatile("mov %%cr3, %0" : "=r"(pdpt_phys));
      pae_pdpt_t *pdpt = (pae_pdpt_t *)pae_map_window((uint64_t)pdpt_phys, 2);
      for (int i = 0; i < 3; i++) {
          if (pdpt->entries[i] & 1) {
              uint32_t pd_phys = (uint32_t)(pdpt->entries[i] & PAE_ADDR_MASK);
              pae_page_directory_t *pd = (pae_page_directory_t *)pae_map_window((uint64_t)pd_phys, 1);
              for (int j = 0; j < 512; j++) {
                  if ((pd->entries[j] & 1) && !(pd->entries[j] & (1ULL << 7))) {
                      uint32_t pt_phys = (uint32_t)(pd->entries[j] & PAE_ADDR_MASK);
                      pae_page_table_t *pt = (pae_page_table_t *)pae_map_window((uint64_t)pt_phys, 0);
                      for (int k = 0; k < 512; k++) {
                          if (pt->entries[k] & 1) {
                              pmm_free_block((void *)(uintptr_t)(pt->entries[k] & PAE_ADDR_MASK));
                              pt->entries[k] = 0;
                          }
                      }
                      pae_unmap_window(0);
                      pmm_free_block((void *)(uintptr_t)pt_phys);
                      pd->entries[j] = 0;
                  }
              }
              pae_unmap_window(1);
          }
      }
      pae_unmap_window(2);
      asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
      return;
  }
  uint32_t phys_pd;
  asm volatile("mov %%cr3, %0" : "=r"(phys_pd));
  uint32_t *pd = (uint32_t *)PHYS_TO_VIRT(phys_pd);
  for (int i = 256; i < 768; i++) {
    if (pd[i] & 1) {
      uint32_t *pt = (uint32_t *)PHYS_TO_VIRT(pd[i] & 0xFFFFF000);
      for (int j = 0; j < 1024; j++) {
        if (pt[j] & 1) {
          pmm_free_block((void *)(uintptr_t)(pt[j] & 0xFFFFF000));
          pt[j] = 0;
        }
      }
      pmm_free_block((void *)(uintptr_t)(pd[i] & 0xFFFFF000));
      pd[i] = 0;
    }
  }
  asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
}
