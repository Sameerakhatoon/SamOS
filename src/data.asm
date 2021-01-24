; src/data.asm - sector 2 of the disk image.
;
; The boot sector reads this sector into 0x7E00 and prints the NUL-terminated
; string at the start.

message: db 'Hello World!!!', 0

times 512-($-$$) db 0
