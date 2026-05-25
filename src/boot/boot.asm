[org 0x7c00]
[bits 16]
    jmp short start
    nop

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE], dl

    ; TRACE B
    mov dx, 0x3F8
    mov al, 'B'
    out dx, al

    ; ── Step 1: E820 Memory Detection ───────────────────────
    call detect_e820

    ; ── Step 2: LBA Availability Check ──────────────────────
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .no_lba
    cmp bx, 0xAA55
    jne .no_lba
    
    ; TRACE L (LBA Supported)
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al
    jmp .load_kernel

.no_lba:
    ; TRACE C (CHS Fallback - but we only have LBA now)
    mov dx, 0x3F8
    mov al, 'C'
    out dx, al
    jmp .error

.load_kernel:
    ; ── Step 3: Load Kernel (LBA) ──────────────────────────
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al

    ; Reset disk
    xor ah, ah
    mov dl, [BOOT_DRIVE]
    int 0x13

    mov word [dap_offset], 0x0000
    mov word [dap_segment], 0x0800
    mov dword [dap_lba_lo], 1
    
    mov cx, 1024                    ; Load first 512KB (Enough for now)
    
.lba_loop:
    push cx
    cmp cx, 64
    jbe .last_chunk
    mov cx, 64
.last_chunk:
    mov [dap_sectors], cx
    
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    mov si, dap
    int 0x13
    jc .error

    ; Trace chunk read success
    mov dx, 0x3F8
    mov al, '*'
    out dx, al

    ; Advance LBA and segment
    movzx eax, word [dap_sectors]
    add [dap_lba_lo], eax
    shl ax, 5                       ; sectors * 32
    add [dap_segment], ax

    pop cx
    sub cx, [dap_sectors]
    jnz .lba_loop

    ; ── TRACE P
    mov dx, 0x3F8
    mov al, 'P'
    out dx, al

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:init_pm

.error:
    mov dx, 0x3F8
    mov al, 'X'
    out dx, al
    cli
    hlt

detect_e820:
    pusha
    mov dx, 0x3F8
    mov al, 'E'
    out dx, al
    
    xor ax, ax
    mov es, ax
    mov di, 0x5000
    xor ebx, ebx
    xor bp, bp
.loop:
    mov eax, 0x0000E820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    inc bp
    add di, 24
    mov dx, 0x3F8
    mov al, '.'
    out dx, al
    test ebx, ebx
    jnz .loop
.done:
    mov [0x4FF0], bp
    popa
    ret

[bits 32]
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov dx, 0x3F8
    mov al, '#'
    out dx, al
    call 0x8000
    hlt

align 4
gdt_dat:
    dq 0
    dw 0xFFFF, 0, 0x9A00, 0x00CF
    dw 0xFFFF, 0, 0x9200, 0x00CF
gdt_desc:
    dw 23
    dd gdt_dat

BOOT_DRIVE  db 0
align 4
dap:
    db 0x10, 0
dap_sectors:  dw 0
dap_offset:   dw 0
dap_segment:  dw 0
dap_lba_lo:   dd 0
dap_lba_hi:   dd 0

    times 510-($-$$) db 0
    dw 0xAA55
