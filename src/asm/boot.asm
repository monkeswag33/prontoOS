%include "src/asm/multiboot_struc.inc"
%define KERNEL_VIRTUAL_ADDR 0xFFFFFFFF80000000
%define PRESENT_BIT 1
%define WRITE_BIT 0b10
%define HUGEPAGE_BIT 0b10000000
%define PAGE_SIZE 0x200000
%define PAGE_TABLE_ENTRY HUGEPAGE_BIT | WRITE_BIT | PRESENT_BIT
%define LOOP_LIMIT 512
[section .multiboot.text]
[bits 32]

global _start
global gdt64
global p4_table
global p3_table
global p2_table
global multiboot_acpi_info
global multiboot_framebuffer_data
global multiboot_basic_meminfo
global multiboot_mmap_data
extern init_kernel

_start:
    mov edi, ebx
    mov esi, eax
    mov esp, stack.top - KERNEL_VIRTUAL_ADDR
    
    mov eax, p3_table - KERNEL_VIRTUAL_ADDR; Copy p3_table address in eax
    or eax, PRESENT_BIT | WRITE_BIT        ; set writable and present bits to 1
    mov dword [(p4_table - KERNEL_VIRTUAL_ADDR) + 0], eax   ; Copy eax content into the entry 0 of p4 table
    mov dword [(p4_table - KERNEL_VIRTUAL_ADDR) + 511 * 8], eax

    mov eax, p2_table - KERNEL_VIRTUAL_ADDR  ; Let's do it again, with p2_table
    or eax, PRESENT_BIT | WRITE_BIT       ; Set the writable and present bits
    mov dword [(p3_table - KERNEL_VIRTUAL_ADDR) + 0], eax   ; Copy eax content in the 0th entry of p3
    mov dword [(p3_table - KERNEL_VIRTUAL_ADDR) + 510 * 8], eax

    mov ecx, 0  ; Loop counter

    .map_p2_table:
        mov eax, PAGE_SIZE  ; Size of the page
        mul ecx             ; Multiply by counter
        or eax, PAGE_TABLE_ENTRY ; We set: huge page bit (if on 2M pages), writable and present 

        ; Moving the computed value into p2_table entry defined by ecx * 8
        ; ecx is the counter, 8 is the size of a single entry
        mov [(p2_table - KERNEL_VIRTUAL_ADDR) + ecx * 8], eax

        inc ecx             ; Let's increase ecx
        cmp ecx, LOOP_LIMIT        ; have we reached 512 ? (1024 for small pages)
                            ; When small pages is enabled:
                            ; each table is 4k size. Each entry is 8bytes
                            ; that is 512 entries in a table
                            ; when small pages enabled: two tables are adjacent in memory
                            ; they are mapped in the pdir during the map_pd_table cycle
                            ; this is why the loop is up to 1024
        
        jne .map_p2_table   ; if ecx < 512 then loop


    ; All set... now we are nearly ready to enter into 64 bit
    ; Is possible to move into cr3 only from another register
    ; So let's move p4_table address into eax first
    ; then into cr3
    mov eax, (p4_table - KERNEL_VIRTUAL_ADDR)
    mov cr3, eax

    ; Now we can enable PAE
    ; To do it we need to modify cr4, so first let's copy it into eax
    ; we can't modify cr registers directly
    mov eax, cr4
    or eax, 1 << 5  ; Physical address extension bit
    mov cr4, eax
    
    ; Now set up the long mode bit
    mov ecx, 0xC0000080
    ; rdmsr is to read a a model specific register (msr)
    ; it copy the values of msr into eax
    rdmsr
    or eax, 1 << 8
    ; write back the value
    wrmsr
    
    ; Now is time to enable paging
    mov eax, cr0    ;cr0 contains the values we want to change
    or eax, 1 << 31 ; Paging bit
    mov cr0, eax    ; write back cr0
    ; load gdt 
    lgdt [gdt64.pointer_low - KERNEL_VIRTUAL_ADDR]
    jmp (0x8):(kernel_jumper - KERNEL_VIRTUAL_ADDR)

section .text
bits 64
kernel_jumper:
    ; update segment selectors
    mov ax, 0x10
    mov ss, ax  ; Stack segment selector
    mov ds, ax  ; data segment register
    mov es, ax  ; extra segment register
    mov fs, ax  ; extra segment register
    mov gs, ax  ; extra segment register
    mov rsp, stack.top
    lea rax, [rdi + 8]
    lgdt [gdt64.pointer]

    push 0x8
    push read_multiboot
    retfq

read_multiboot:
    mov ebx, [rax + multiboot_tag.type]
    cmp ebx, MULTIBOOT_TAG_TYPE_FRAMEBUFFER
    je .multiboot_framebuffer
    cmp ebx, MULTIBOOT_TAG_TYPE_MMAP
    je .multiboot_mmap
    cmp ebx, MULTIBOOT_TAG_TYPE_BASIC_MEMINFO
    je .multiboot_meminfo
    cmp ebx, MULTIBOOT_TAG_TYPE_ACPI_OLD
    je .multiboot_acpi
    cmp ebx, MULTIBOOT_TAG_TYPE_ACPI_NEW
    je .multiboot_acpi

    jmp .skip_item

    .multiboot_framebuffer:
        mov [multiboot_framebuffer_data], rax
        jmp .skip_item

    .multiboot_mmap:
        mov [multiboot_mmap_data], rax
        jmp .skip_item

    .multiboot_meminfo:
        mov [multiboot_basic_meminfo], rax
        jmp .skip_item

    .multiboot_acpi:
        mov [multiboot_acpi_info], rax

    .skip_item:
        xor rbx, rbx
        mov ebx, [rax + multiboot_tag.size]
        add rax, rbx
        add rax, 7
        and rax, ~7
        cmp dword [rax + multiboot_tag.type], MULTIBOOT_TAG_TYPE_END
        jne read_multiboot
        cmp dword [rax + multiboot_tag.size], 8
        jne read_multiboot
    call init_kernel

section .bss

align 4096
p4_table: ;PML4
    resb 4096
p3_table: ;PDPR
    resb 4096
p2_table: ;PDP
    resb 4096
multiboot_framebuffer_data:
    resb 8
multiboot_mmap_data:
    resb 8
multiboot_basic_meminfo:
    resb 8
multiboot_acpi_info:
    resb 8
stack:
    resb 16384
    .top:

section .rodata

; gdt table needs at least 3 entries:
;     the first entry is always null
;     the other two are data segment and code segment.
gdt64:
    dq  0
    .kernel_code equ $ - gdt64
        dq (1 << 53) | (1 << 47) | (1 <<44) | (1 << 43) | (1 << 41)
        ; Long mode code segment, Present, Code or data segment, Code segment, Read access

    .kernel_data equ $ - gdt64
        dq (1 << 47) | (1 << 44) | (1 << 41)
        ; Present, Code or data segment, data segment, Read & Write
    
    .user_code equ $ - gdt64
        dq (1 << 53) | (1 << 47) | (3 << 45) | (1 << 43) | (1 << 41)
        ; Long mode code segment, User DPL, Present, Code or data segment, Code segment, Read access
    
    .user_data equ $ - gdt64
        dq (1 << 47) | (3 << 45) | (1 << 44) | (1 << 41)
        ; Present, User DPL, Code or data segment, data segment, Read & Write

.pointer:
    dw .pointer - gdt64 - 1
    dq gdt64
.pointer_low:
    dw .pointer - gdt64 - 1
    dq gdt64 - KERNEL_VIRTUAL_ADDR