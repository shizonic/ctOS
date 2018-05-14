/*
 * pci.c
 *
 * This is the PCI bus driver which is responsible for enumerating PCI devices at boot time and
 * presenting PCI devices to device drivers
 */

#include "pci.h"
#include "io.h"
#include "locks.h"
#include "debug.h"
#include "mm.h"
#include "lists.h"
#include "keyboard.h"
#include "vga.h"
#include "cpu.h"

static char* __module = "IDE   ";


/*
 * These pointers point to head and tail of a list
 * of PCI devices
 */
static pci_dev_t* pci_dev_list_head=0;
static pci_dev_t* pci_dev_list_tail=0;

/*
 * List of PCI busses in the system
 */
static pci_bus_t* pci_bus_list_head=0;
static pci_bus_t* pci_bus_list_tail=0;

/*
 * PCI config spaces are accessed via a index/data
 * register pair. Thus concurrent reads or writes can lead to
 * wrong data. To avoid this, we use a spinlock to protect these
 * registers
 */
static spinlock_t pci_config_reg_lock;

/*
 * A table of some known classes.
 * See for instance http://www.pcidatabase.com for a full list
 */

static pci_class_t pci_class_codes[] = { {
        0x00,
        0x00,
        0x00,
        "Pre 2.0 - Non-VGA" }, { 0x00, 0x01, 0x00, "Pre 2.0 - VGA" }, {
        0x01,
        0x00,
        0x00,
        "Storage - SCSI" }, { 0x01, 0x01, 0xff, "Storage - IDE" }, {
        0x01,
        0x02,
        0x00,
        "Storage - Floppy" }, { 0x01, 0x03, 0x00, "Storage - IPI" }, {
        0x01,
        0x04,
        0x00,
        "Storage - RAID" }, { 0x01, 0x06, 0x01, "SATA - AHCI" }, {
        0x01,
        0x80,
        0x00,
        "Storage - Other" }, { 0x02, 0x00, 0x00, "Ethernet" }, {
        0x02,
        0x01,
        0x00,
        "Token Ring" }, { 0x03, 0x00, 0x00, "Display - VGA" }, {
        0x03,
        0x00,
        0x01,
        "Display - 8514" }, { 0x03, 0x01, 0x00, "Display - XGA" }, {
        0x03,
        0x80,
        0x00,
        "Display - Other" }, { 0x04, 0x03, 0xff, "Audio" }, {
        0x06,
        0x00,
        0x00,
        "Host/PCI Bridge" }, { 0x06, 0x01, 0x00, "PCI/ISA Bridge" }, {
        0x06,
        0x02,
        0x00,
        "PCI/EISA Bridge" }, { 0x06, 0x03, 0x00, "PCI/MCA Bridge" }, {
        0x06,
        0x04,
        0xff,
        "PCI/PCI Bridge" }, { 0x06, 0x05, 0x00, "PCI/PCMCIA Bridge" }, {
        0x06,
        0x80,
        0x00,
        "Bridge - Other" }, { 0x08, 0x00, 0x00, "PIC 8259" }, {
        0x08,
        0x00,
        0x01,
        "PIC ISA" }, { 0x08, 0x00, 0x02, "PIC PCI" }, {
        0x08,
        0x00,
        0x20,
        "I/O APIC" }, { 0x08, 0x01, 0x00, "DMA 8259" }, {
        0x08,
        0x01,
        0x01,
        "DMA ISA" }, { 0x08, 0x01, 0x02, "DMA EISA" }, {
        0x08,
        0x02,
        0x00,
        "Timer 8259" }, { 0x08, 0x02, 0x01, "Timer ISA" }, {
        0x08,
        0x02,
        0x02,
        "Timer EISA" }, { 0x08, 0x03, 0x00, "RTC Generic" }, {
        0x08,
        0x03,
        0x01,
        "RTC ISA" }, { 0x0C, 0x00, 0xff, "Firewire (IEEE 1394)" }, {
        0x0C,
        0x03,
        0x00,
        "USB Controller" }, { 0x0C, 0x03, 0x20, "USB EHCI" }, {
        0x0C,
        0x03,
        0x30,
        "USB XHCI" }, { 0x0C, 0x5, 0xff, "SMBus" } };
        
/*
 * Valid capabilities
 */

static capability_t capabilities[] = { 
            { 1, "Power Management" }, 
            { 2, "AGP" },
            { 3, "VPD" }, 
            { 4, "Slot" }, 
            { 5, "MSI" },
            { 6, "Compact PCI Hot Swap" }, 
            { 7, "PCI-X" }, 
            { 0xc, "PCI HotPlug" },
            {0x10, "PCI Express"}, 
            {0x11, "MSI-X"}, 
            {0x12, "SATA"}} ;        


#define CAPABILITY_LIST_SIZE (sizeof(capabilities) / sizeof(capability_t))
#define PCI_CLASS_CODE_SIZE (sizeof(pci_class_codes) / sizeof(pci_class_t))

/*
 * Read four bytes from the PCI configuration space
 * Parameter:
 * @bus: bus specifying the device to read from
 * @device: device number to read from
 * @function: function number to read from
 * @offset: offset into 256-byte config space (sometimes called register),
 *          should be a multiple of 4, if not it is rounded down to
 *          a multiple of 4 by settings bits 0 and 1 to 0
 * Return value:
 * a dword containing the content of the register
 * Locks:
 * pci_config_reg_lock
 */
static u32 pci_get_dword_config(u8 bus, u8 device, u8 function, u8 offset) {
    /*
     * We get the configuration byte using method 1 - write to 0xcf8
     * and read from 0xcfd
     * Using this method, we read 4 configuration bytes at a time,
     * i.e. if offset is 4, we actually read from registers 4,5,6
     * and 7 in one transaction
     * The double word which we have to write to 0xcf8 is build up
     * as follows
     * Bit 31: set to 1 (enable configuration read/write)
     * Bit 16-23: bus number (8 bit)
     * Bit 11-15: device number (5 bit, i.e. 0-31)
     * Bit 8-10: function number (3 bit, i.e. 0-7)
     * Bit 2-7: bits 2-7 of offset
     * Bit 0-1: always zero
     */
    u32 eflags;
    u8 offset_base;
    u32 address;
    u32 data;
    /*
     * Set bits 0 and 1 to 0
     */
    offset_base = offset & 0xfc;
    /*
     * Now assemble address to be written to 0xcf8
     * Recall that the PCI configuration space protocol specifies
     * that this is composed of bus, device, function and offset
     */
    address = 0x80000000 + (((u32) bus) << 16)
            + ((((u32) device) & 0x1f) << 11) + ((((u32) function) & 0x7) << 8)
            + offset_base;
    /*
     * Write to port and read result
     */
    spinlock_get(&pci_config_reg_lock, &eflags);
    outl(address, PCI_CONFIG_ADDRESS);
    data = inl(PCI_CONFIG_DATA);
    spinlock_release(&pci_config_reg_lock, &eflags);
    return data;
}

/*
 * Get an individual word from an individual register
 * as opposed to the full 32-bit dword (4 registers) returned
 * by the function pci_get_dword_config
 * Parameter:
 * @bus: bus specifying the device to read from
 * @device: device number to read from
 * @function: function number to read from
 * @offset: offset into 256-byte config space (sometimes called register),
 * Return value:
 * a word containing the content of the register

 */
static u16 pci_get_word_config(u8 bus, u8 device, u8 function, u8 offset) {
    u32 dword_pci_value;
    /*
     * Position of word we are looking for in 32-bit dword
     * returned by pci_get_dword_config
     */
    int position = offset % 4;
    dword_pci_value = pci_get_dword_config(bus, device, function, offset);
    return (u16) (dword_pci_value >> (8 * position));
}

/*
 * Get an individual word from an individual register
 * as opposed to the full 32-bit dword (4 registers) returned
 * by the function pci_get_dword_config
 * Parameter:
 * @bus: bus specifying the device to read from
 * @device: device number to read from
 * @function: function number to read from
 * @offset: offset into 256-byte config space (sometimes called register),
 * Return value:
 * a byte containing the content of the register

 */
static u8 pci_get_byte_config(u8 bus, u8 device, u8 function, u8 offset) {
    u32 dword_pci_value;
    /*
     * Position of word we are looking for in 32-bit dword
     * returned by pci_get_dword_config
     */
    int position = offset % 4;
    dword_pci_value = pci_get_dword_config(bus, device, function, offset);
    return (u8) (dword_pci_value >> (8 * position));
}
/*
 * Write four bytes to the PCI configuration space
 * Parameter:
 * @bus: bus specifying the device to read from
 * @device: device number to read from
 * @function: function number to read from
 * @offset: offset into 256-byte config space (sometimes called register),
 *          should be a multiple of 4, if not it is rounded down to
 *          a multiple of 4 by settings bits 0 and 1 to 0
 * @value: a dword containing the content of the register
 * Locks:
 * pci_config_reg_lock
 */
static void pci_put_dword_config(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 eflags;
    u8 offset_base;
    u32 address;
    /*
     * Set bits 0 and 1 to 0
     */
    offset_base = offset & 0xfc;
    /*
     * Now assemble address to be written to 0xcf8
     * Recall that the PCI configuration space protocol specifies
     * that this is composed of bus, device, function and offset
     */
    address = 0x80000000 + (((u32) bus) << 16)
            + ((((u32) device) & 0x1f) << 11) + ((((u32) function) & 0x7) << 8)
            + offset_base;
    /*
     * Write to both ports
     */
    spinlock_get(&pci_config_reg_lock, &eflags);
    outl(address, PCI_CONFIG_ADDRESS);
    outl(value, PCI_CONFIG_DATA);
    spinlock_release(&pci_config_reg_lock, &eflags);
    return;
}

/* Put an individual word into an individual register
 * as opposed to the full 32-bit dword (4 registers) written
 * by the function pci_put_dword_config
 * Note that offset needs to be even for this to work!
 */
static void pci_put_word_config(u8 bus, u8 device, u8 function, u8 offset, u16 value) {
    u32 dword_pci_value;
    /* 
     * Position of byte we are looking for in 32-bit dword
     * returned by pci_get_dword_config
     */
    int position = offset % 4;
    /* 
     * First read old value 
     */
    dword_pci_value = pci_get_dword_config(bus, device, function,
            offset);
    /* 
     * Now patch requested byte into value 
     */
    dword_pci_value = dword_pci_value & ~(0xffff << (8*position));
    dword_pci_value = dword_pci_value | (((u32)value) << (8*position));
    pci_put_dword_config(bus, device, function, offset, dword_pci_value);
}

/*
 * Locate bus by id in internal table
 * Parameter:
 * @bus_id - the bus ID
 * Return value:
 * a pointer to the bus or 0 if bus could not be found
 */
static pci_bus_t* pci_get_bus_for_id(u8 bus_id) {
    pci_bus_t* pci_bus;
    LIST_FOREACH(pci_bus_list_head, pci_bus) {
        if (bus_id == pci_bus->bus_id) {
            return pci_bus;
        }
    }
    return 0;
}

/*
 * Scan a given device and fill the pci device structure
 * passed as argument accordingly
 * Parameters:
 * @bus_id - the bus id
 * @device - the device
 * @function - the function
 * @pci_dev - the function to be filled
 */
static void pci_scan_device(u8 bus_id, u8 device, u8 function,
        pci_dev_t* pci_dev) {
    u32 command_status;
    u32 vendor_device_id;
    u8 primary_bus;
    u8 secondary_bus;
    u8 cap_pointer;
    u8 cap_id;
    int i = 0;
    /*
     * Fill structure. This is not very efficient, as we read several
     * 32 bit registers more than once, but lets keep things simple...
     */
    vendor_device_id = pci_get_dword_config(bus_id, device, function,
            PCI_HEADER_VENDOR_DEVID_REG);
    pci_dev->device = device;
    pci_dev->function = function;
    pci_dev->vendor_id = (u16) vendor_device_id;
    pci_dev->device_id = (u16) (vendor_device_id >> 16);
    pci_dev->base_class = pci_get_byte_config(bus_id, device, function,
            PCI_HEADER_BASECLASS_REG);
    pci_dev->sub_class = pci_get_byte_config(bus_id, device, function,
            PCI_HEADER_SUBCLASS_REG);
    pci_dev->prog_if = pci_get_byte_config(bus_id, device, function,
            PCI_HEADER_PROGIF_REG);
    pci_dev->header = pci_get_byte_config(bus_id, device, function,
            PCI_HEADER_TYPE_REG);
    pci_dev->msi_support = 0;
    /*
     * Read command and status register
     * We access these registers as one double word at offset 0x4
     */
    command_status = pci_get_dword_config(bus_id, pci_dev->device,
            pci_dev->function, PCI_HEADER_COMMAND_REG);
    pci_dev->command = command_status;
    pci_dev->status = (u16) (command_status >> 16);
    if (PCI_HEADER_GENERAL_DEVICE == (pci_dev->header & 0x3)) {
        /*
         * Its a general device. Read bars
         */
        for (i = 0; i < 6; i++)
            pci_dev->bars[i] = pci_get_dword_config(bus_id, device, function, i
                    * 4 + PCI_HEADER_BAR0);
    }
    pci_dev->irq_line = pci_get_byte_config(bus_id, device, function,
            PCI_HEADER_IRQ_LINE_REG);
    pci_dev->irq_pin = pci_get_byte_config(bus_id, device, function,
            PCI_HEADER_IRQ_PIN_REG);
    /*
     * If there is a capability list, read it
     */
    if (pci_dev->status & PCI_STATUS_CAP_LIST) {
        cap_pointer = pci_get_byte_config(pci_dev->bus->bus_id,
                                        pci_dev->device, 
                                        pci_dev->function, 
                                        PCI_HEADER_CAP_POINTER_REG);
        while (cap_pointer) {
            cap_id = pci_get_byte_config(pci_dev->bus->bus_id,
                    pci_dev->device, pci_dev->function, cap_pointer);
            if (PCI_CAPABILITY_MSI == cap_id) {
                pci_dev->msi_support = 1;
                pci_dev->msi_cap_offset = cap_pointer;
            }
            cap_pointer = pci_get_byte_config(pci_dev->bus->bus_id,
                    pci_dev->device, pci_dev->function, cap_pointer + 1);
        }
    }
    if (PCI_HEADER_PCI_BRIDGE == (pci_dev->header & 0x3)) {
        /*
         * We hit upon a PCI-to-PCI bridge. Get information on primary and secondary bus
         * from bridge-specific registers. The secondary bus is the "new" bus
         */
        primary_bus = pci_get_byte_config(bus_id, device, function,
                PCI_HEADER_PRIMARY_BUS);
        secondary_bus = pci_get_byte_config(bus_id, device, function,
                PCI_HEADER_SECONDARY_BUS);
        pci_dev->primary_bus = primary_bus;
        pci_dev->secondary_bus = secondary_bus;
    }
    else {
        pci_dev->primary_bus = 0;
        pci_dev->secondary_bus = 0;
    }
}

/*
 * Scan a bus and add the devices which we have found
 * to the list of known devices. When hitting upon a PCI-PCI bridge
 * check whether the bus behind the bridge appears in the list of
 * known busses and add it to the tail of the list if not
 * Parameter:
 * @pci_bus - the bus to be scanned
 */
static void pci_scan_bus(pci_bus_t* pci_bus) {
    u8 device;
    u8 function;
    u32 vendor_device_id;
    pci_dev_t* pci_dev;
    pci_bus_t* secondary_pci_bus;

    u8 bus_id = pci_bus->bus_id;
    for (device = 0; device <= 31; device++) {
        for (function = 0; function <= 7; function++) {
            /*
             * First read vendor id - if that is 0xffff,
             * there is no device for that dev/func
             */
            vendor_device_id = pci_get_dword_config(bus_id, device, function,
                    PCI_HEADER_VENDOR_DEVID_REG);
            if (0xffffffff != vendor_device_id) {
                /*
                 * Seems to be an existing device - set up
                 * pci_dev structure for it and read more data
                 */
                pci_dev = (pci_dev_t*) kmalloc(sizeof(pci_dev_t));
                if (0 == pci_dev) {
                    PANIC("Could not allocate memory for PCI device\n");
                    return;
                }
                pci_bus->devfunc_count++;
                pci_dev->bus = pci_bus;
                pci_scan_device(bus_id, device, function, pci_dev);
                if (PCI_HEADER_PCI_BRIDGE == (pci_dev->header & 0x3)) {
                    /*
                     * This is a bridge to another bus. Check whether this bus already exists and set
                     * it up if not
                     */
                    if (0 == pci_get_bus_for_id(pci_dev->secondary_bus)) {
                        secondary_pci_bus = (pci_bus_t*) kmalloc(
                                sizeof(pci_bus_t));
                        if (0 == secondary_pci_bus) {
                            PANIC("Could not allocate memory for pci bus\n");
                            return;
                        }
                        secondary_pci_bus->bus_id = pci_dev->secondary_bus;
                        secondary_pci_bus->devfunc_count = 0;
                        LIST_ADD_END(pci_bus_list_head, pci_bus_list_tail, secondary_pci_bus);
                    }
                }
                /*
                 * Add device structure to list
                 */
                LIST_ADD_END(pci_dev_list_head, pci_dev_list_tail, pci_dev);
                /*
                 * If we have just processed function 0, read bit 7 of the header type
                 * to find out whether the device is a multifunction device.
                 * If not, skip remaining functions to avoid duplicates
                 */
                if (0 == function) {
                    if (!(PCI_HEADER_MF_MASK & pci_dev->header)) {
                        break;
                    }
                }
            }
        }
    }
}

/*
 * Initialize the PCI bus driver and perform a
 * scan of the PCI bus system to detect busses and
 * devices
 */
void pci_init() {
    pci_bus_t* pci_bus;
    /*
     * Reset lists
     */
    pci_bus_list_head = 0;
    pci_bus_list_tail = 0;
    pci_dev_list_head = 0;
    pci_dev_list_tail = 0;
    /*
     * Init spin lock to protect registers
     */
    spinlock_init(&pci_config_reg_lock);
    /*
     * Set up one bus entry - this is the entry for bus 0
     */
    pci_bus_list_head = (pci_bus_t*) kmalloc(sizeof(pci_bus_t));
    if (0 == pci_bus_list_head) {
        PANIC("No memory available for PCI bus list\n");
        return;
    }
    pci_bus_list_head->bus_id = 0;
    pci_bus_list_head->next = 0;
    pci_bus_list_head->prev = 0;
    pci_bus_list_head->devfunc_count = 0;
    pci_bus_list_tail = pci_bus_list_head;
    /*
     * Now scan first bus. This function will add additional items to
     * the list of busses in case it hits upon bridges on the bus
     * currently being scanned, so we will stay in the loop until
     * the scan does not reveal any additional busses
     */
    pci_bus = pci_bus_list_head;
    while (pci_bus) {
        pci_scan_bus(pci_bus);
        pci_bus = pci_bus->next;
    }
}


/*
 * This function calls the provided callback once for
 * each registered PCI device
 * Parameter:
 * @callback - the callback to use
 */
void pci_query_all(pci_query_callback_t callback) {
    pci_dev_t* pci_dev;
    LIST_FOREACH(pci_dev_list_head, pci_dev) {
        callback(pci_dev);
    }
}

/*
 * This function calls the provided callback once for
 * each registered PCI device which matches the provided
 * base class
 * Parameter:
 * @callback - the callback to use
 * @base_class - the base class
 */
void pci_query_by_baseclass(pci_query_callback_t callback, u8 base_class) {
    pci_dev_t* pci_dev;
    LIST_FOREACH(pci_dev_list_head, pci_dev) {
        if (pci_dev->base_class==base_class)
            callback(pci_dev);
    }
}

/*
 * This function calls the provided callback once for
 * each registered PCI device which matches the provided
 * base class and sub class
 * Parameter:
 * @callback - the callback to use
 * @base_class - the base class
 * @sub_class -the sub class
 */
void pci_query_by_class(pci_query_callback_t callback, u8 base_class, u8 sub_class) {
    pci_dev_t* pci_dev;
    LIST_FOREACH(pci_dev_list_head, pci_dev) {
        if ((pci_dev->base_class==base_class) && (pci_dev->sub_class==sub_class))
            callback(pci_dev);
    }
}


/*
 * Get the PCI status register from offset 0x6 of configuration space
 */
u16 pci_get_status(pci_dev_t* pci_dev) {
    u32 command_status;
    command_status = pci_get_dword_config(pci_dev->bus->bus_id, pci_dev->device,
                pci_dev->function, PCI_HEADER_COMMAND_REG);
    return (u16) (command_status >> 16);
}

/*
 * Get the PCI command register 
 */
u16 pci_get_command(pci_dev_t* pci_dev) {
    u32 command_status;
    command_status = pci_get_dword_config(pci_dev->bus->bus_id, pci_dev->device,
                pci_dev->function, PCI_HEADER_COMMAND_REG);
    return (u16) (command_status);
}
 
/*
 * Enable the PCI bus master functionality by setting the
 * bus master configuration bit
 */
void pci_enable_bus_master_dma(pci_dev_t* pci_dev) {
     /*
     * First get the current value of the command / status register
     */
    u32 value;
    value = pci_get_dword_config(pci_dev->bus->bus_id, pci_dev->device,
            pci_dev->function, PCI_HEADER_COMMAND_REG);
    /*
     * If bit 2 is already set we are fine
     */
    if (value & PCI_COMMAND_BUS_MASTER) {
        return;
    }
    MSG("Bus master DMA not yet enabled - writing configuration bit\n");
    /*
     * Now set bit 2 of the command register
     */
    value = value | PCI_COMMAND_BUS_MASTER;
    /*
     * and write back
     */
    pci_put_dword_config(pci_dev->bus->bus_id, pci_dev->device,
            pci_dev->function, PCI_HEADER_COMMAND_REG, value);
    /*
     * Did it work?
     */
    value = pci_get_dword_config(pci_dev->bus->bus_id, pci_dev->device,
            pci_dev->function, PCI_HEADER_COMMAND_REG);
    if (0 == (value & PCI_COMMAND_BUS_MASTER)) {
        PANIC("Could not set bus master configuration bit, giving up\n");
    }
    return;
}

/*
 * This function reads the MSI configuration for a given device
 * Parameter:
 * @dev - the device
 * @msi_config - the MSI config structure that we fill
 */
static void pci_get_msi_config(pci_dev_t* dev, msi_config_t* msi_config) {
    u16 msg_control;
    msg_control = pci_get_dword_config(dev->bus->bus_id, 
                                dev->device, 
                                dev->function, dev->msi_cap_offset) >> 16;
    msi_config->is64 = (msg_control & PCI_MSI_64_SUPP) / PCI_MSI_64_SUPP;
    msi_config->masking = (msg_control & PCI_MSI_MASKING_SUPP) / PCI_MSI_MASKING_SUPP;
    msi_config->msg_cap = (msg_control >> 1) & 0x7;
    msi_config->msg_enabled=(msg_control >> 4) & 0x7;
    msi_config->msg_address = pci_get_dword_config(dev->bus->bus_id,
                                dev->device,
                                dev->function,
                                dev->msi_cap_offset + 4);
    msi_config->msi_enabled = msg_control & PCI_MSI_CNTL_ENABLED;
    if (msi_config->is64) {
        msi_config->msg_address_upper = pci_get_dword_config(dev->bus->bus_id,
                                dev->device,
                                dev->function,
                                dev->msi_cap_offset + 8);
        msi_config->msg_data =  pci_get_word_config(dev->bus->bus_id,
                                dev->device,
                                dev->function,
                                dev->msi_cap_offset + 12);
    }
    else {
        msi_config->msg_address_upper = 0;
        msi_config->msg_data = pci_get_word_config(dev->bus->bus_id,
                                dev->device,
                                dev->function,
                                dev->msi_cap_offset + 8);
    }
}

/*
 * Write configuration information to a given device
 * Out of the msi config structure passed as argument, only
 * the fields
 *   message address
 *   message data
 *   Number of messages enabled in message control register
 *   MSI enable flag in message control register
 * are actually written. However, the remaining fields are assumed
 * to be valid and should be the result of a previous read operation
 */
static void pci_put_msi_config(pci_dev_t* dev, msi_config_t* msi_config) {
    u8 msi_offset = dev->msi_cap_offset;
    u16 msg_control;
    msg_control = pci_get_dword_config(dev->bus->bus_id, 
                                    dev->device,
                                    dev->function, 
                                    msi_offset) >> 16;
    msg_control = msg_control | (msi_config->msi_enabled & 0x1);
    msg_control = msg_control & ~(0x7 << 4);
    msg_control = msg_control | ((msi_config->msg_enabled & 0x7) << 4);
    pci_put_word_config(dev->bus->bus_id, dev->device, dev->function,
                                msi_offset+2,
                                msg_control);
    pci_put_dword_config(dev->bus->bus_id,
                                dev->device,
                                dev->function,
                                msi_offset + 4, 
                                msi_config->msg_address);
    if (msi_config->is64) {
        pci_put_dword_config(dev->bus->bus_id,
                    dev->device,
                    dev->function,
                    msi_offset + 8, 
                    msi_config->msg_address_upper);
        pci_put_word_config(dev->bus->bus_id,
                    dev->device,
                    dev->function,
                    msi_offset + 12, 
                    msi_config->msg_data);

    }
    else {
        pci_put_word_config(dev->bus->bus_id,
                    dev->device,
                    dev->function,
                    msi_offset + 8, 
                    msi_config->msg_data);
    }
}

/*
 * Set up a device for MSI. We do not check any more that 
 * the device supports this, but assume that this has been 
 * done before
 * Parameter:
 * @pci_dev - the device
 * @vector - the vector we want to use
 * TODO: support different delivery modes
 */
void pci_config_msi(pci_dev_t* pci_dev, int vector) {
    msi_config_t msi_config;
    /*
     * Read existing configuration first
     */
    pci_get_msi_config(pci_dev, &msi_config);
    msi_config.msi_enabled = 1;
    /*
     * Build message address.
     * Bits 20-31: 0xfee
     * Bits 12-19: local APIC ID
     * Bits 0-11: zero (use physical destination mode)
     */
    msi_config.msg_address = 0xfee00000 + ((cpu_get_apic_id(0) & 0xf) << 12);
    msi_config.msg_address_upper = 0;
    /*
     * Build message data
     * Bits 0-8: vector 
     * Bits 8-10: zero for vector based delivery
     * Bit 14: set to one
     */
    msi_config.msg_data = vector + (1<<14);
    /*
     * Write configuration data back
     */
    pci_put_msi_config(pci_dev, &msi_config);
}

/******************************************************************
 * The functions below this line are for debugging purpose only   *
 * and are meant to be used by the internal debugger              *
 ******************************************************************/


/*
 * Return a short description for the device,
 * based on class codes
 * Parameters:
 * @base_class - base class of device
 * @sub_class - sub class of device
 * @prog_if - programming interface of device
 * Return value:
 * a string describing the type of the device
 */
static char* get_desc_for_cc(u8 base_class, u8 sub_class, u8 prog_if) {
    int i = 0;
    while (i < PCI_CLASS_CODE_SIZE) {
        if ((pci_class_codes[i].base_class == base_class)
                && (pci_class_codes[i].sub_class == sub_class)
                && ((pci_class_codes[i].prog_if == prog_if)
                        || (pci_class_codes[i].prog_if == 0xff))) {
            return pci_class_codes[i].desc;
        }
        i++;
    }
    return "Unknown";
}

/*
 * Return the name of a capability
 */
char* get_capability_name(u8 capability) {
    int i = 0;
    while (i < CAPABILITY_LIST_SIZE) {
        if (capabilities[i].id == capability) {
            return capabilities[i].name;
        }
        i++;
    }
    return "Unknown";

}

/*
 * Format output for a base address register (BAR)
 * Parameters:
 * @bar - the BAR content
 * @nr - the number of the register
 */
static void pci_print_bar(u32 bar, int nr) {
    u32 address;
    u32 type;
    u32 io;
    io = bar & BAR_IO_SPACE;
    if (!io) {
        address = bar & 0xfffffff0;
        type = bar & BAR_TYPE;
        PRINT("BAR%d=%x (MEM,type=%h)    ", nr, address, type);
    }
    else {
        address = bar & 0xfffffffc;
        PRINT("BAR%d=%x (I/O)            ", nr, address);
    }
    if (0==((nr+1) % 2))
        PRINT("\n");
}


/*
 * Print details for a PCI device
 * Parameter:
 * @pci_dev - the device
 */
static void pci_print_device_details(pci_dev_t* pci_dev) {
    int i;
    u32 bar32;
    u32 command_status;
    u16 command;
    u16 status;
    u8 cap_pointer;
    u8 cap_id;
    cls(0);
    PRINT("Details on device %h:%h.%h\n", pci_dev->bus->bus_id, pci_dev->device,
            pci_dev->function);
    PRINT("------------------------------------\n");
    PRINT("Vendor ID: %w  Device ID: %w\n", pci_dev->vendor_id,
            pci_dev->device_id);
    PRINT("Class code: base=%h, subclass=%h, programming interface=%h\n",
            pci_dev->base_class, pci_dev->sub_class, pci_dev->prog_if);
    PRINT("Class description: %s\n", get_desc_for_cc(pci_dev->base_class,
                    pci_dev->sub_class, pci_dev->prog_if));
    PRINT("MSI:             %d\n", pci_dev->msi_support);
    PRINT("IRQ line:        %h  IRQ pin:       %h\n", pci_dev->irq_line,
            pci_dev->irq_pin);
    command_status = pci_get_dword_config(pci_dev->bus->bus_id, pci_dev->device,
            pci_dev->function, PCI_HEADER_COMMAND_REG);
    command = command_status;
    status = (u16) (command_status >> 16);
    PRINT("Header type:     %h  Status:        %w     Command: %w\n",
            pci_dev->header, status, command);
    if (PCI_HEADER_PCI_BRIDGE == (pci_dev->header & 0x3)) {
        PRINT("Primary bus: %h  Secondary bus: %h\n",
                pci_dev->primary_bus, pci_dev->secondary_bus);
    }
    if (PCI_HEADER_GENERAL_DEVICE == (pci_dev->header & 0x3)) {
        for (i = 0; i < 6; i++) {
            bar32 = pci_get_dword_config(pci_dev->bus->bus_id, pci_dev->device,
                    pci_dev->function, PCI_HEADER_BAR0 + 4 * i);
            pci_print_bar(bar32, i);
        }
    }
    PRINT("Access via I/O space enabled: %h\n", (pci_dev->command
                    & PCI_COMMAND_IO_ENABLED) / PCI_COMMAND_IO_ENABLED);
    PRINT("Access via memory space enabled: %h\n", (pci_dev->command
                    & PCI_COMMAND_MEM_ENABLED) / PCI_COMMAND_MEM_ENABLED);
    PRINT("Capability list present: %h\n", (pci_dev->status
                    & PCI_STATUS_CAP_LIST) / PCI_STATUS_CAP_LIST);
    if (pci_dev->status & PCI_STATUS_CAP_LIST) {
        PRINT("ID      Name\n");
        PRINT("---------------------\n");
        cap_pointer = pci_get_byte_config(pci_dev->bus->bus_id,
                                        pci_dev->device, 
                                        pci_dev->function, 
                                        PCI_HEADER_CAP_POINTER_REG);
        while (cap_pointer) {
            cap_id = pci_get_byte_config(pci_dev->bus->bus_id,
                    pci_dev->device, pci_dev->function, cap_pointer);
            PRINT("%h      %s\n", cap_id, get_capability_name(cap_id));
            cap_pointer = pci_get_byte_config(pci_dev->bus->bus_id,
                    pci_dev->device, pci_dev->function, cap_pointer + 1);
        }
    }
    else {
        kprintf("This device does not implement any capabilities\n");
    }                    
    PRINT("Hit any key to return to list\n");
    early_getchar();
}

/*
 * Print some information on a given device
 * Parameter:
 * @nr - the position on the screen
 * @pci_dev - the device to be printed
 */
static void pci_print_device_summary(int nr, pci_dev_t* pci_dev) {
    PRINT("%h  %h   %h.%h     %w    %w    %w   %w   %w  %s\n", nr,
            pci_dev->bus->bus_id, pci_dev->device, pci_dev->function,
            pci_dev->vendor_id, pci_dev->device_id, pci_dev->base_class,
            pci_dev->sub_class, pci_dev->prog_if, get_desc_for_cc(
                    pci_dev->base_class, pci_dev->sub_class, pci_dev->prog_if));
}

/*
 * List all PCI devices on the busses scanned so far
 */
void pci_list_devices() {
    pci_dev_t* pci_dev;
    pci_dev = pci_dev_list_head;
    int nr = 1;
    char input;
    /*
     * This is an array containing pointers to the devices
     * listed on the current page. Used to branch to the details page
     * In particular, the entry at index 1 is a pointer to the first
     * device on the current page
     */
    pci_dev_t * shown_devices[DEVICE_LIST_PAGE_SIZE + 1];
    cls(0);
    PRINT("         Device/   Vendor  Device  Base   Sub    Prog  Class      \n");
    PRINT("Nr  Bus  Function  ID      ID      Class  Class  If    Description\n");
    PRINT("-------------------------------------------------------------------------\n");
    while (pci_dev) {
        pci_print_device_summary(nr, pci_dev);
        shown_devices[nr] = pci_dev;
        pci_dev = pci_dev->next;
        nr++;
        if ((nr > DEVICE_LIST_PAGE_SIZE) || (!pci_dev)) {
            /* Reached end of page */
            nr--;
            if (pci_dev)
                PRINT(
                        "Hit a number to display details or any other key to proceed to next page\n");
            else
                PRINT(
                        "Hit a number to display details or any other key to return to prompt\n");
            input = early_getchar();
            if ((input >= '1') && (input <= ('1' + nr))) {
                /* Its a number between 1 and the number of displayed items */
                pci_print_device_details(shown_devices[input - '0']);
                /* Reset pointer so that we start printing at the first device on
                 * the current page again
                 */
                pci_dev = shown_devices[1];
            }
            /* If there are entries left,  set up header for next page */
            if (pci_dev) {
                nr = 1;
                cls(0);
                PRINT("         Device/   Vendor  Device  Base   Sub    Prog            \n");
                PRINT("Nr  Bus  Function  ID      ID      Class  Class  If    Description\n");
                PRINT("-------------------------------------------------------------------------\n");
            }
        }
    }
}
