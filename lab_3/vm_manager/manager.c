#include "manager.h"
#include <time.h>

int kvmfd, vmfd, vcpufd;
struct kvm_run *run;
uint8_t *memory;
int slot_id = 0;
int create_vm(void)
{
    int ret;

    /* Getting the KVM File Descriptor */
    kvmfd = open("/dev/kvm", O_RDWR);
    if (kvmfd == -1)
        err(1, "/dev/kvm");
    ret = ioctl(kvmfd, KVM_GET_API_VERSION, NULL);
    if (ret == -1)
        err(1, "KVM_GET_API_VERSION");

    /* Creating A VM Structure */
    vmfd = ioctl(kvmfd, KVM_CREATE_VM, (unsigned long)0);
    if (vmfd == -1)
        err(1, "KVM_init_vm");
    return 0;
}
int add_memory(size_t size, uint64_t start)
{
    int ret = -1;
    /* Allocating The Guest Physical Memory */
    memory = (uint8_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!memory)
        err(1, "allocating guest memory code\n");
    struct kvm_userspace_memory_region guest_region = {
        .slot = slot_id++,
        .guest_phys_addr = start,
        .memory_size = size,
        .userspace_addr = (uint64_t)memory,
    };
    ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &guest_region);
    if (ret == -1)
        err(1, "KVM_SET_USER_MEMORY_REGION");
    return ret;
}

int create_bootstrap()
{
    struct kvm_sregs sregs;
    size_t mmap_size;
    int ret = -1;
    /* Creating One vCPU */
    int nent = 128;
    struct kvm_cpuid2 *cpuid2 = (struct kvm_cpuid2 *)malloc(sizeof(struct kvm_cpuid2) + nent * sizeof(struct kvm_cpuid_entry2));
    cpuid2->nent = nent;
    if (ioctl(kvmfd, KVM_GET_SUPPORTED_CPUID, cpuid2) < 0)
        err(1, "cant get cpuid");

    vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, (unsigned long)0);

    if (vcpufd == -1)
        err(1, "Cannot create vcpu\n");

    if (ioctl(vcpufd, KVM_SET_CPUID2, cpuid2) < 0)
        err(1, "cannot set cpuid things\n");

    /* Map the shared kvm_run structure and following data. */
    ret = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, NULL);
    if (ret == -1)
        err(1, "KVM_GET_VCPU_MMAP_SIZE");
    mmap_size = ret;
    if (mmap_size < sizeof(*run))
        errx(1, "KVM_GET_VCPU_MMAP_SIZE unexpectedly small");
    run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
    if (!run)
        err(1, "mmap vcpu");

    /* Initializing vCPU Registers */
    ret = ioctl(vcpufd, KVM_GET_SREGS, &sregs);
    if (ret == -1)
        err(1, "KVM_GET_SREGS");
    sregs.cs.base = 0;
    sregs.cs.selector = 0;
    ret = ioctl(vcpufd, KVM_SET_SREGS, &sregs);
    if (ret == -1)
        err(1, "KVM_SET_SREGS");
    return ret;
}

int launch_vm(uint64_t boot_rip, uint64_t app_rip, uint64_t sp)
{

    int ret;
    /* Updating vCPU Registers */
    struct kvm_regs regs = {
        .rip = boot_rip, /* Setting The RIP Register To The Bootstrap Entry Point */
        .rflags = 0x2,
        .r15 = app_rip, /* User Application Entry Point */
        .r14 = sp, /* User Application Stack Starting Address */
    };
    ret = ioctl(vcpufd, KVM_SET_REGS, &regs);
    if (ret == -1)
        err(1, "KVM_SET_REGS");

    while (1)
    {
        if (ioctl(vcpufd, KVM_RUN, NULL) == -1)
            err(1, "KVM_RUN");
        else
        {
            if (vmexit_handler(run->exit_reason) == 0)
                break;
        }
    }
    return 0;
}

int vmexit_handler(int exit_reason)
{
    switch (exit_reason)
    {
    case KVM_EXIT_HLT:
        {
            int result = syscall_handler(memory, vcpufd);
            if (result == 0)
            {
                /* SAVE operation requested - dump VM state and exit */
                if (dump_vm_state(VM_IMAGE_FILENAME, 0xF000) == 0)
                {
                    exit(0);
                }
                else
                {
                    fprintf(stderr, "Failed to save VM state\n");
                    exit(1);
                }
            }
            return result;
        }
    case KVM_EXIT_IO:
        if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == 0x3f8 && run->io.count == 1)
        {
            printf("KVM_EXIT_IO: ");
            putchar(*(((char *)run) + run->io.data_offset));
            printf("\n");
            return 0;
        }
        else
            errx(1, "unhandled KVM_EXIT_IO %d", run->io.port == 0x3f8);
        break;
    case KVM_EXIT_FAIL_ENTRY:
        errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx",
             (unsigned long long)run->fail_entry.hardware_entry_failure_reason);
    case KVM_EXIT_INTERNAL_ERROR:
        errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x", run->internal.suberror);
    case KVM_EXIT_SHUTDOWN:
        errx(1, "SHUTDOWN");
        break;
    default:
        errx(1, "exit_reason = 0x%d", exit_reason);
    }
    return 1;
}

uint8_t *get_memory()
{
    return memory;
}

/* ========== VM Migration Implementation - Step 1: Dump Functions ========== */

/* Dump vCPU General Purpose Registers */
int dump_vcpu_regs(int vcpufd, FILE *fp)
{
    struct kvm_regs regs;
    
    if (ioctl(vcpufd, KVM_GET_REGS, &regs) == -1)
    {
        perror("KVM_GET_REGS");
        return -1;
    }
    
    if (fwrite(&regs, sizeof(struct kvm_regs), 1, fp) != 1)
    {
        perror("fwrite regs");
        return -1;
    }
    
    return 0;
}

/* Dump vCPU Special Registers (CR0, CR3, CR4, segments, etc.) */
int dump_vcpu_sregs(int vcpufd, FILE *fp)
{
    struct kvm_sregs sregs;
    
    if (ioctl(vcpufd, KVM_GET_SREGS, &sregs) == -1)
    {
        perror("KVM_GET_SREGS");
        return -1;
    }
    
    if (fwrite(&sregs, sizeof(struct kvm_sregs), 1, fp) != 1)
    {
        perror("fwrite sregs");
        return -1;
    }
    
    return 0;
}

/* Dump vCPU FPU/SSE State */
int dump_vcpu_fpu(int vcpufd, FILE *fp)
{
    struct kvm_fpu fpu;
    
    if (ioctl(vcpufd, KVM_GET_FPU, &fpu) == -1)
    {
        perror("KVM_GET_FPU");
        return -1;
    }
    
    if (fwrite(&fpu, sizeof(struct kvm_fpu), 1, fp) != 1)
    {
        perror("fwrite fpu");
        return -1;
    }
    
    return 0;
}

/* Dump Guest Physical Memory */
int dump_guest_memory(uint8_t *memory, uint64_t size, FILE *fp)
{
    if (fwrite(memory, 1, size, fp) != size)
    {
        perror("fwrite memory");
        return -1;
    }
    
    return 0;
}

/* Dump Device State (open file descriptors) */
int dump_device_state(FILE *fp)
{
    int num_fds = 0;  // No FDs tracked yet
    
    if (fwrite(&num_fds, sizeof(int), 1, fp) != 1)
    {
        perror("fwrite num_fds");
        return -1;
    }
    
    return 0;
}

/* Dump Pending IO Requests */
int dump_io_requests(FILE *fp)
{
    int num_ios = 0;  // No pending IOs yet
    
    if (fwrite(&num_ios, sizeof(int), 1, fp) != 1)
    {
        perror("fwrite num_ios");
        return -1;
    }
    
    return 0;
}

/* High-level function to dump entire VM state */
int dump_vm_state(const char *filename, uint64_t memory_size)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp)
    {
        perror("fopen dump");
        return -1;
    }
    
    /* Write VM image header */
    vm_image_header_t header;
    header.magic = VM_IMAGE_MAGIC;
    header.version = VM_IMAGE_VERSION;
    header.memory_size = memory_size;
    header.num_open_fds = 0;
    header.timestamp = (uint64_t)time(NULL);
    header.num_pending_io = 0;
    
    if (fwrite(&header, sizeof(vm_image_header_t), 1, fp) != 1)
    {
        perror("fwrite header");
        fclose(fp);
        return -1;
    }
    
    /* Dump all VM components */
    if (dump_vcpu_regs(vcpufd, fp) < 0 ||
        dump_vcpu_sregs(vcpufd, fp) < 0 ||
        dump_vcpu_fpu(vcpufd, fp) < 0 ||
        dump_guest_memory(memory, memory_size, fp) < 0 ||
        dump_device_state(fp) < 0 ||
        dump_io_requests(fp) < 0)
    {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    return 0;
}