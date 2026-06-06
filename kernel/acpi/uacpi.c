#include <uacpi/platform/types.h>
#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/uacpi.h>
#include <uacpi/event.h>
#include <uacpi/types.h>
#ifdef __x86_64__
#include <kernel/arch/x86_64/io.h>
#endif
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/sched.h>
#include <kernel/time.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    extern uint64_t acpi_get_rsdp(void);
    *out_rsdp_address = acpi_get_rsdp();
    return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    uacpi_phys_addr phys = ALIGN_DOWN(addr, PAGE_SIZE);
    for (size_t i = 0; i < len; i += PAGE_SIZE) {
        mmu_map(kernel_pd, VIRTUAL_HHDM(phys + i), (void *)(phys + i), flags);
    }

    return VIRTUAL_HHDM(addr);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    (void)addr;
    (void)len;
}

void uacpi_kernel_log(uacpi_log_level loglevel, const uacpi_char *str) {
    int level = 0;
    switch (loglevel) {
        case UACPI_LOG_DEBUG:
            return;
        case UACPI_LOG_TRACE:
        case UACPI_LOG_INFO:
            level = LOG_DEBUG;
            break;
        case UACPI_LOG_WARN:
            level = LOG_WARNING;
            break;
        case UACPI_LOG_ERROR:
            level = LOG_ERR;
            break;
        default:
            level = LOG_DEBUG;
            break;
    }

    dprintf(level, "\033[93macpi:\033[0m %s", str);
}

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
    uacpi_pci_address *addr = kmalloc(sizeof(uacpi_pci_address));
    memcpy(addr, &address, sizeof(uacpi_pci_address));
    *out_handle = addr;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    kfree(handle);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *value) {
    uacpi_pci_address *addr = (uacpi_pci_address *)handle;
    *value = (pci_read((pci_address){ addr->bus, addr->device, addr->function }, offset & ~3) >> ((offset & 3) * 8)) & 0xFF;
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *value) {
    uacpi_pci_address *addr = (uacpi_pci_address *)handle;
    *value = pci_config_read_word((pci_address){ addr->bus, addr->device, addr->function }, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *value) {
    uacpi_pci_address *addr = (uacpi_pci_address *)handle;
    *value = pci_read((pci_address){ addr->bus, addr->device, addr->function }, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value) {
    uacpi_pci_address *addr = (uacpi_pci_address *)handle;
    uint32_t dword = pci_read((pci_address){ addr->bus, addr->device, addr->function }, offset & ~3);
    uint8_t shift = (offset & 3) * 8;
    pci_write((pci_address){ addr->bus, addr->device, addr->function }, offset & ~3, (dword & ~(0xFF << shift)) | ((uint32_t)value << shift));
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value) {
    uacpi_pci_address *addr = (uacpi_pci_address *)handle;
    pci_config_write_word((pci_address){ addr->bus, addr->device, addr->function }, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
    uacpi_pci_address *addr = (uacpi_pci_address *)handle;
    pci_write((pci_address){ addr->bus, addr->device, addr->function }, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
    (void)len;
    *out_handle = (uacpi_handle)base;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    (void)handle;
}

#ifdef __x86_64__
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {
    uacpi_io_addr port = (uacpi_io_addr)(handle + offset);
    *out_value = inb(port);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {
    uacpi_io_addr port = (uacpi_io_addr)(handle + offset);
    *out_value = inw(port);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {
    uacpi_io_addr port = (uacpi_io_addr)(handle + offset);
    *out_value = inl(port);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    uacpi_io_addr port = (uacpi_io_addr)(handle + offset);
    outb(port, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    uacpi_io_addr port = (uacpi_io_addr)(handle + offset);
    outw(port, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    uacpi_io_addr port = (uacpi_io_addr)(handle + offset);
    outl(port, in_value);
    return UACPI_STATUS_OK;
}
#endif

void *uacpi_kernel_alloc(uacpi_size n) {
    return kmalloc(n);
}

void uacpi_kernel_free(void *ptr) {
    if (ptr)
        kfree(ptr);
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    uacpi_u64 nsec = 0;
    uptime(NULL, &nsec);
    return nsec;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    arch_sleep(usec * 1000);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    arch_sleep(msec * 1000000);
}

uacpi_handle uacpi_kernel_create_spinlock(void);
void uacpi_kernel_free_spinlock(uacpi_handle);

uacpi_handle uacpi_kernel_create_mutex(void) {
    return uacpi_kernel_create_spinlock();
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    uacpi_kernel_free_spinlock(handle);
}

uacpi_handle uacpi_kernel_create_event(void) {
    return (uacpi_handle)1;
}

void uacpi_kernel_free_event(uacpi_handle handle) {
    (void)handle;
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return get_core(get_logical_id())->current_tcb;
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    assert(timeout == 0xffff);
    acquire(handle);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    release(handle);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
    (void)handle;
    (void)timeout;
    return UACPI_TRUE;
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
    (void)handle;
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
    (void)handle;
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {
    (void)req;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle *out_irq_handle) {
    (void)irq;
    (void)handler;
    (void)ctx;
    (void)out_irq_handle;
    *out_irq_handle = handler;
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
    (void)handler;
    (void)irq_handle;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock_t *lock = kmalloc(sizeof(spinlock_t));
    *lock = 0;
    return (uacpi_handle)lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    kfree(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    acquire(handle);
    return 0;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    (void)flags;
    release(handle);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
    (void)type;
    (void)handler;
    (void)ctx;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_init(void) {
    uacpi_status ret = uacpi_initialize(0);
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m uacpi_initialize error: %s\n", uacpi_status_to_string(ret));
        return;
    }

    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m uacpi_namespace_load error: %s\n", uacpi_status_to_string(ret));
        return;
    }

    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m uacpi_namespace_initialize error: %s\n", uacpi_status_to_string(ret));
        return;
    }

    ret = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m uACPI GPE initialization error: %s\n", uacpi_status_to_string(ret));
        return;
    }
}