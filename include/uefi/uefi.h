#pragma once
#include <stdint.h>
#include <stddef.h>

#define ERR_MASK ((uint64_t) 1 << 63)
#define ERR(status) (status | ERR_MASK)
#define EFI_ERR(status) (status & ERR_MASK)

struct efi_so;
struct efi_si;
struct mmap_desc;
struct boot_info;

typedef uint64_t efi_status_t;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} efi_alloc_type_t;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiUnacceptedMemoryType,
    EfiMaxMemoryType
} efi_mem_type_t;

typedef enum {
    AllHandles,
    ByRegisterNotify,
    ByProtocol
} efi_locate_search_type_t;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid_t;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t rsv;
} efi_table_header_t;

typedef struct {
    efi_table_header_t header;

    uintptr_t raise_tpl;
    uintptr_t restore_tpl;

    efi_status_t (*alloc_pages)(efi_alloc_type_t, efi_mem_type_t, size_t, uintptr_t*);
    efi_status_t (*free_pages)(uintptr_t, size_t);
    efi_status_t (*get_mmap)(size_t*, struct mmap_desc*, size_t*, size_t*, uint32_t*);
    efi_status_t (*alloc_pool)(efi_mem_type_t, size_t, void**);
    efi_status_t (*free_pool)(void*);

    uintptr_t create_event;
    uintptr_t set_timer;
    uintptr_t wait_for_event;
    uintptr_t signal_event;
    uintptr_t close_event;
    uintptr_t check_event;

    uintptr_t install_protocol_interface;
    uintptr_t reinstall_protocol_interface;
    uintptr_t uninstall_protocol_interface;
    efi_status_t (*handle_protocol)(void*, efi_guid_t*, void**);
    uintptr_t pc_handle_protocol;
    uintptr_t register_protocol_notify;
    uintptr_t locate_handle;
    uintptr_t locate_device_path;
    uintptr_t install_config_table;

    uintptr_t load_image;
    uintptr_t start_image;
    uintptr_t exit;
    uintptr_t unload_image;
    efi_status_t (*exit_boot_services)(void*, size_t);

    uintptr_t get_next_monotonic_count;
    uintptr_t stall;
    efi_status_t (*set_watchdog_timer)(size_t, uint64_t, size_t, uint16_t*);

    uintptr_t connect_controller;
    uintptr_t disconnect_controller;

    uintptr_t open_protocol;
    uintptr_t close_protocol;
    uintptr_t open_protocol_information;

    efi_status_t (*protocols_per_handle)(void*, efi_guid_t***, size_t*);
    efi_status_t (*locate_handle_buffer)(efi_locate_search_type_t, efi_guid_t*, void*, size_t*, void***);
    efi_status_t (*locate_protocol)(const efi_guid_t*, void*, void**);
    uintptr_t install_multiple_protocol_interfaces;
    uintptr_t uninstall_multiple_protocol_interfaces;

    efi_status_t (*calculate_crc32)(void*, size_t, uint32_t*);

    void (*copy_mem)(void*, void*, size_t);
    void (*set_mem)(void*, size_t, uint8_t);
    uintptr_t create_event_ex;
} efi_boot_services_t;

typedef struct {
    efi_guid_t vendor_guid;
    void *vendor_table;
} efi_config_table_t;

typedef struct {
    efi_table_header_t header;

    uint16_t *firmware_vendor;
    uint32_t firmware_revision;

    void *console_in_handle;
    struct efi_si *con_in;

    void *console_out_handle;
    struct efi_so *con_out;

    void *standard_error_handle;
    struct efi_so *std_err;

    uintptr_t rs;

    efi_boot_services_t *bs;

    size_t table_entries;
    efi_config_table_t *config_table;
} efi_system_table_t;

extern efi_system_table_t *gST;
extern struct boot_info gBI;