#ifndef __MANAGER__
#define __MANAGER__
#pragma once
#include "../load_manager/loader.h"
#include "../syscall_manager/syscall_handler.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#define DEV_KVM "/dev/kvm"

#define TSS_OFSETT 0x5000

struct InterruptDescriptor64
{
	uint16_t offset_1;		 // offset bits 0..15
	uint16_t selector;		 // a code segment selector in GDT or LDT
	uint8_t ist;			 // bits 0..2 holds Interrupt Stack Table offset, rest of bits zero.
	uint8_t type_attributes; // gate type, dpl, and p fields
	uint16_t offset_2;		 // offset bits 16..31
	uint32_t offset_3;		 // offset bits 32..63
	uint32_t zero;			 // reserved
};

struct idt_ptr
{
	unsigned short limit;
	unsigned long long base;
} __attribute__((packed));

struct tss_entry_struct
{
	uint32_t prev_tss; // The previous TSS - with hardware task switching these form a kind of backward linked list.
	uint32_t esp0;	   // The stack pointer to load when changing to kernel mode.
	uint32_t ss0;	   // The stack segment to load when changing to kernel mode.
	// Everything below here is unused.
	uint32_t esp1; // esp and ss 1 and 2 would be used when switching to rings 1 or 2.
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	uint32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldt;
	uint16_t trap;
	uint16_t iomap_base;
} __attribute__((packed));
typedef struct tss_entry_struct tss_entry_t;

int create_vm(void);
int add_memory(size_t, uint64_t);
int create_bootstrap();
int launch_vm(uint64_t, uint64_t, uint64_t);
int vmexit_handler(int);
uint8_t *get_memory(void);
int setup_tss(uint32_t);
int ident_paging(uint8_t *);
void setup_idt();

/* ========== VM Migration - Step 1 ========== */

/* VM Image Format Definitions */
#define VM_IMAGE_MAGIC 0x564D494D47  // "VMIMG" in hex
#define VM_IMAGE_VERSION 1
#define VM_IMAGE_FILENAME "vm_state.img"

/* VM Image Header - Contains metadata about the saved VM state */
typedef struct {
    uint64_t magic;              // Magic number to identify VM image file
    uint32_t version;            // Version of the image format
    uint64_t memory_size;        // Size of guest physical memory
    uint32_t num_open_fds;       // Number of open file descriptors
    uint64_t timestamp;          // Timestamp when image was created
    uint32_t num_pending_io;     // Number of pending IO requests
} vm_image_header_t;

/* Function prototypes for dumping VM components */
int dump_vcpu_regs(int vcpufd, FILE *fp);
int dump_vcpu_sregs(int vcpufd, FILE *fp);
int dump_vcpu_fpu(int vcpufd, FILE *fp);
int dump_guest_memory(uint8_t *memory, uint64_t size, FILE *fp);
int dump_device_state(FILE *fp);
int dump_io_requests(FILE *fp);

/* High-level function to dump entire VM state */
int dump_vm_state(const char *filename, uint64_t memory_size);


/* Function prototypes for restoring VM components */
int restore_vcpu_regs(int vcpufd, FILE *fp);
int restore_vcpu_sregs(int vcpufd, FILE *fp);
int restore_vcpu_fpu(int vcpufd, FILE *fp);
int restore_guest_memory(uint8_t *memory, uint64_t size, FILE *fp);
int restore_device_state(FILE *fp);
int restore_io_requests(FILE *fp);

/* High-level function to restore entire VM state */
int restore_vm_state(const char *filename, uint64_t memory_size);
/* Global variables for migration */
extern int kvmfd, vmfd, vcpufd;
extern struct kvm_run *run;
extern uint8_t *memory;

#endif
