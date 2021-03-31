; src/boot/boot.asm - load the kernel (100 sectors starting at LBA 1) into
; 0x100000 via ATA PIO, then jump to it.

ORG 0x7C00
BITS 16

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

_start:
    jmp short start
    nop

; FAT16 BIOS Parameter Block (offset 3..35, 33 bytes)
OEMIdentifier       db 'SAMOS   '       ; 8 bytes
BytesPerSector      dw 0x200
SectorsPerCluster   db 0x80
ReservedSectors     dw 200
FATCopies           db 0x02
RootDirEntries      dw 0x40
NumSectors          dw 0x00
MediaType           db 0xF8
SectorsPerFat       dw 0x100
SectorsPerTrack     dw 0x20
NumberOfHeads       dw 0x40
HiddenSectors       dd 0x00
SectorsBig          dd 0x773594

; Extended BPB (DOS 4.0, offset 36..61, 26 bytes)
DriveNumber         db 0x80
WinNTBit            db 0x00
Signature           db 0x29
VolumeID            dd 0xD105
VolumeIDString      db 'SAMOS BOO  '    ; 11 bytes
SystemIDString      db 'FAT16   '       ; 8 bytes

start:
    jmp 0:step2

step2:
    cli
    mov ax, 0x00
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

.load_protected:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:load32

; ---- GDT --------------------------------------------------------------------
gdt_start:
gdt_null:
    dd 0x0
    dd 0x0

; offset 0x08: kernel code segment, base 0, limit 0xFFFFF (G=1 -> 4 GiB).
gdt_code:
    dw 0xFFFF
    dw 0
    db 0
    db 0x9A
    db 11001111b
    db 0

; offset 0x10: kernel data segment.
gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 0x92
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---- 32-bit code ------------------------------------------------------------
[BITS 32]
load32:
    ; Enable the A20 line.
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load kernel: 100 sectors starting at LBA 1 (sector 0 is this boot sector)
    ; into 0x100000.
    mov eax, 1                  ; starting LBA
    mov ecx, 100                ; sectors to read
    mov edi, 0x0100000          ; destination buffer
    call ata_lba_read

    ; Jump to the kernel. CODE_SEG ensures CS reloads with the kernel code
    ; selector before we run any of the kernel's instructions.
    jmp CODE_SEG:0x0100000

; ---- ata_lba_read -----------------------------------------------------------
; in:  eax = LBA, ecx = sector count, edi = dest buffer
; clobbers: eax, ebx, ecx, edx
ata_lba_read:
    mov ebx, eax                ; backup the LBA

    ; Send the top 8 bits of the LBA plus drive-select bits to port 0x1F6.
    shr eax, 24
    or eax, 0xE0                ; 0xE0 = master + LBA mode
    mov dx, 0x1F6
    out dx, al

    ; Sector count -> 0x1F2.
    mov eax, ecx
    mov dx, 0x1F2
    out dx, al

    ; LBA byte 0 -> 0x1F3.
    mov eax, ebx
    mov dx, 0x1F3
    out dx, al

    ; LBA byte 1 -> 0x1F4.
    mov dx, 0x1F4
    mov eax, ebx
    shr eax, 8
    out dx, al

    ; LBA byte 2 -> 0x1F5.
    mov dx, 0x1F5
    mov eax, ebx
    shr eax, 16
    out dx, al

    ; READ SECTORS command (0x20) -> 0x1F7.
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al

.next_sector:
    push ecx

.try_again:
    mov dx, 0x1F7
    in al, dx
    test al, 8                  ; DRQ bit, set when data is ready
    jz .try_again

    ; Pull 256 16-bit words (= 512 bytes = one sector) from port 0x1F0 to [es:di].
    mov ecx, 256
    mov dx, 0x1F0
    rep insw

    pop ecx
    loop .next_sector
    ret

times 510-($-$$) db 0
dw 0xAA55
