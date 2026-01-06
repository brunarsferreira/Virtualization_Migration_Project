#include "../../vm_manager/manager.h"
#include "../../load_manager/loader.h"
#include "../../utils/util.h"
#include "../../memory_manager/memory.h"

#define VM_MEMORY_SIZE  0xF000
#define STACK_ADDRESS   0xE000
#define STACK_SIZE      0x3000

/* TODO */
int main(int argc, char *argv)
{
    /* Create a blank VM */
    create_vm();
    create_bootstrap();
    add_memory(VM_MEMORY_SIZE, 0);
    
    /* Read the VM image file and update the VM state */
    if (restore_vm_state(VM_IMAGE_FILENAME, VM_MEMORY_SIZE) < 0)
    {
        fprintf(stderr, "Failed to restore VM state\n");
        return 1;
    }
    
    /* Launch the VM (resume execution) */
    printf("VM_APP - RESTORE OPERATION\n");
    
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
