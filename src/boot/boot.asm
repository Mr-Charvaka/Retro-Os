[org 0x7c00]
    jmp short start
    nop

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov bp, 0x7C00
    mov sp, bp
    mov [BOOT_DRIVE], dl

    ; Load 1100 Sectors (~550KB)
    mov ax, 0x800 ; Segment 0x800 -> Offset 0x8000
    mov es, ax
    xor bx, bx
    mov cx, 1100 ; Sectors to read
    mov byte [LBA_ABS_S], 2 ; Sector 2 (Kernel start)
    mov byte [LBA_ABS_H], 0
    mov byte [LBA_ABS_C], 0

.read_loop:
    push cx
    mov ah, 0x02
    mov al, 1
    mov cl, [LBA_ABS_S]
    mov ch, [LBA_ABS_C]
    mov dh, [LBA_ABS_H]
    mov dl, [BOOT_DRIVE]
    int 0x13
    
    add bx, 512
    jnc .skip_inc_seg
    mov ax, es
    add ax, 0x1000
    mov es, ax
.skip_inc_seg:
    
    inc byte [LBA_ABS_S]
    cmp byte [LBA_ABS_S], 63
    jbe .next
    mov byte [LBA_ABS_S], 1
    inc byte [LBA_ABS_H]
    cmp byte [LBA_ABS_H], 16
    jb .next
    mov byte [LBA_ABS_H], 0
    inc byte [LBA_ABS_C]
.next:
    pop cx
    loop .read_loop

    ; Switch to Protected Mode
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:init_pm_bin

[bits 32]
init_pm_bin:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    call 0x8000 ; Start Kernel
    jmp $

gdt_dat:
    dq 0 ; null
    dw 0xffff, 0, 0x9a00, 0x00cf ; code
    dw 0xffff, 0, 0x9200, 0x00cf ; data
gdt_desc:
    dw 23 ; 3 * 8 - 1
    dd gdt_dat

LBA_ABS_S db 0
LBA_ABS_H db 0
LBA_ABS_C db 0
BOOT_DRIVE db 0

    times 446-($-$$) db 0
    ; Partition 1
    db 0x80, 0, 1, 0, 0x0C, 0, 0, 0
    dd 2048, 63488
    
    times 16 * 3 db 0
    dw 0xAA55
