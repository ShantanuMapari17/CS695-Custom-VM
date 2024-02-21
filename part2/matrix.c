#include <stdio.h>
#include <sys/mman.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <bits/types/timer_t.h>

#define KVM_DEVICE "/dev/kvm"
#define RAM_SIZE 512000000
#define CODE_START 0x1000
#define BINARY_FILE1 "guest1.bin"
#define BINARY_FILE2 "guest2.bin"
#define CURRENT_TIME ((double)clock() / CLOCKS_PER_SEC)

#define SIG SIGUSR1
#define CLOCKID CLOCK_MONOTONIC
#define QUANTUM 1
#define FRAC_A  0.7
#define FRAC_B  0.3



//write signal handler
static void handler(int sig, siginfo_t *si, void *uc)
{
    timer_t *tidp;

    tidp = si->si_value.sival_ptr;

    // printf("Caught signal %d\n", sig);
    // printf("    *sival_ptr         = %ld\n", (long)*tidp);
    // signal(sig, SIG_IGN);
}

struct vm
{
    int dev_fd;
    int kvm_version;
    int vm_fd;
    struct kvm_userspace_memory_region mem;
    struct vcpu *vcpus;
    __u64 ram_size;
    __u64 ram_start;
    int vcpu_number;
};

struct vcpu
{
    int vcpu_id;
    int vcpu_fd;
    pthread_t vcpu_thread;
    struct kvm_run *kvm_run;
    int kvm_run_mmap_size;
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    void *(*vcpu_thread_func)(void *);
};

void kvm_init(struct vm *vm1, struct vm *vm2)
{
    int dev_fd = open(KVM_DEVICE, O_RDWR);

    if (dev_fd < 0)
    {
        perror("open /dev/kvm");
        exit(1);
    }

    int kvm_version = ioctl(dev_fd, KVM_GET_API_VERSION, 0);

    if (kvm_version < 0)
    {
        perror("KVM_GET_API_VERSION");
        exit(1);
    }

    if (kvm_version != KVM_API_VERSION)
    {
        fprintf(stderr, "Got KVM api version %d, expected %d\n",
                kvm_version, KVM_API_VERSION);
        exit(1);
    }

    vm1->dev_fd = dev_fd;
    vm2->dev_fd = dev_fd;
    vm1->kvm_version = kvm_version;
    vm2->kvm_version = kvm_version;
}

int kvm_create_vm(struct vm *vm, int ram_size)
{
    int ret = 0;
    vm->vm_fd = ioctl(vm->dev_fd, KVM_CREATE_VM, 0);

    if (vm->vm_fd < 0)
    {
        perror("can not create vm");
        return -1;
    }

    vm->ram_size = ram_size;
    vm->ram_start = (__u64)mmap(NULL, vm->ram_size,
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                                -1, 0);

    if ((void *)vm->ram_start == MAP_FAILED)
    {
        perror("can not mmap ram");
        return -1;
    }

    vm->mem.slot = 0;
    vm->mem.guest_phys_addr = 0;
    vm->mem.memory_size = vm->ram_size;
    vm->mem.userspace_addr = vm->ram_start;

    ret = ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(vm->mem));

    if (ret < 0)
    {
        perror("can not set user memory region");
        return ret;
    }

    return ret;
}

void load_binary(struct vm *vm, char *binary_file)
{
    int fd = open(binary_file, O_RDONLY);

    if (fd < 0)
    {
        fprintf(stderr, "can not open binary file\n");
        exit(1);
    }

    int ret = 0;
    char *p = (char *)vm->ram_start;

    while (1)
    {
        ret = read(fd, p, 4096);
        if (ret <= 0)
        {
            break;
        }
        printf("VMFD: %d, Loaded Program with size: %d\n", vm->vm_fd, ret);
        p += ret;
    }
}

struct vcpu *kvm_init_vcpu(struct vm *vm, int vcpu_id, void *(*fn)(void *))
{
    struct vcpu *vcpu = malloc(sizeof(struct vcpu));
    vcpu->vcpu_id = vcpu_id;
    vcpu->vcpu_fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, vcpu->vcpu_id);

    if (vcpu->vcpu_fd < 0)
    {
        perror("can not create vcpu");
        return NULL;
    }

    vcpu->kvm_run_mmap_size = ioctl(vm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

    if (vcpu->kvm_run_mmap_size < 0)
    {
        perror("can not get vcpu mmsize");
        return NULL;
    }

    vcpu->kvm_run = mmap(NULL, vcpu->kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->vcpu_fd, 0);

    if (vcpu->kvm_run == MAP_FAILED)
    {
        perror("can not mmap kvm_run");
        return NULL;
    }

    vcpu->vcpu_thread_func = fn;
    return vcpu;
}

void kvm_reset_vcpu(struct vcpu *vcpu)
{
    if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0)
    {
        perror("can not get sregs\n");
        exit(1);
    }

    vcpu->sregs.cs.selector = CODE_START;
    vcpu->sregs.cs.base = CODE_START * 16;
    vcpu->sregs.ss.selector = CODE_START;
    vcpu->sregs.ss.base = CODE_START * 16;
    vcpu->sregs.ds.selector = CODE_START;
    vcpu->sregs.ds.base = CODE_START * 16;
    vcpu->sregs.es.selector = CODE_START;
    vcpu->sregs.es.base = CODE_START * 16;
    vcpu->sregs.fs.selector = CODE_START;
    vcpu->sregs.fs.base = CODE_START * 16;
    vcpu->sregs.gs.selector = CODE_START;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0)
    {
        perror("can not set sregs");
        exit(1);
    }

    vcpu->regs.rflags = 0x0000000000000002ULL;
    vcpu->regs.rip = 0;
    vcpu->regs.rsp = 0xffffffff;
    vcpu->regs.rbp = 0;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0)
    {
        perror("KVM SET REGS\n");
        exit(1);
    }
}

void *kvm_cpu_thread(void *data)
{
    // Copy the code from this function to your code implementation in kvm_run_vm() and modify it accordingly
    struct vm *vm = (struct vm *)data;
    int ret = 0;
    kvm_reset_vcpu(vm->vcpus);

    
    printf("VMFD: %d started running\n", vm->vm_fd);
    ret = ioctl(vm->vcpus->vcpu_fd, KVM_RUN, 0);

    printf("VMFD: %d stopped running - exit reason: %d\n", vm->vm_fd, vm->vcpus->kvm_run->exit_reason);

    switch (vm->vcpus->kvm_run->exit_reason)
    {
    case KVM_EXIT_UNKNOWN:
        printf("VMFD: %d KVM_EXIT_UNKNOWN\n", vm->vm_fd);
        break;
    case KVM_EXIT_DEBUG:
        printf("VMFD: %d KVM_EXIT_DEBUG\n", vm->vm_fd);
        break;
    case KVM_EXIT_IO:
        printf("VMFD: %d KVM_EXIT_IO\n", vm->vm_fd);
        printf("VMFD: %d out port: %d, data: %d\n", vm->vm_fd, vm->vcpus->kvm_run->io.port, *(int *)((char *)(vm->vcpus->kvm_run) + vm->vcpus->kvm_run->io.data_offset));
        sleep(1);
        break;
    case KVM_EXIT_MMIO:
        printf("VMFD: %d KVM_EXIT_MMIO\n", vm->vm_fd);
        break;
    case KVM_EXIT_INTR:
        printf("VMFD: %d KVM_EXIT_INTR\n", vm->vm_fd);
        break;
    case KVM_EXIT_SHUTDOWN:
        printf("VMFD: %d KVM_EXIT_SHUTDOWN\n", vm->vm_fd);
        goto exit_kvm;
        break;
    default:
        printf("VMFD: %d KVM PANIC\n", vm->vm_fd);
        printf("VMFD: %d KVM exit reason: %d\n", vm->vm_fd, vm->vcpus->kvm_run->exit_reason);
        goto exit_kvm;
    }

    if (ret < 0 && vm->vcpus->kvm_run->exit_reason != KVM_EXIT_INTR)
    {
        fprintf(stderr, "VMFD: %d KVM_RUN failed\n", vm->vm_fd);
        printf("VMFD: %d KVM_RUN return value %d\n", vm->vm_fd, ret);
        exit(1);
    }
    else if(ret<0 && vm->vcpus->kvm_run->exit_reason == KVM_EXIT_INTR){
        // printf("Interrupted occured\n");
        // printf("VMFD: %d KVM_RUN failed\n", vm->vm_fd);
        // printf("VMFD: %d KVM_RUN return value %d\n", vm->vm_fd, ret);
    }
    

    exit_kvm:
        return 0;
}

void setTimer(int turn){
    timer_t timerid;
    sigset_t mask;
    struct sigevent sev;
    struct sigaction sa;
    struct itimerspec its;

    /* Establish handler for timer signal. */
    // printf("Establishing handler for signal %d\n", SIG);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG, &sa, NULL) == -1)
        perror("sigaction");

    /* Block timer signal temporarily. */
    // printf("Blocking signal %d\n", SIG);
    sigemptyset(&mask);
    sigaddset(&mask, SIG);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1)
        perror("sigprocmask");

    /* Create the timer. */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCKID, &sev, &timerid) == -1)
        perror("timer_create");

    // printf("timer ID is %d\n", (uintmax_t) timerid);

    /* Start the timer. */
    
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 1e9 * QUANTUM * (turn==1?FRAC_A:FRAC_B);
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(timerid, 0, &its, NULL) == -1)
         perror("timer_settime");

    

    /* Unlock the timer signal, so that timer notification
              can be delivered. */

    // printf("Unblocking signal %d\n", SIG);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
        perror("sigprocmask");

    
}

void kvm_run_vm(struct vm *vm1, struct vm *vm2)
{
    int turn=1;    
    
    while(1){
        setTimer(turn);
        printf("Time: %f\n", CURRENT_TIME);
        if(turn==1){
            kvm_cpu_thread(vm1);
            turn=2;
        }
        else{
            kvm_cpu_thread(vm2);
            turn=1;
        }
    }

    // Remove everything in the function above this line and replace it with your code here

}

void kvm_clean_vm(struct vm *vm)
{
    close(vm->vm_fd);
    munmap((void *)vm->ram_start, vm->ram_size);
}

void kvm_clean_vcpu(struct vcpu *vcpu)
{
    munmap(vcpu->kvm_run, vcpu->kvm_run_mmap_size);
    close(vcpu->vcpu_fd);
}

void kvm_clean(struct vm *vm)
{
    assert(vm != NULL);
    close(vm->dev_fd);
    free(vm);
}

int main(int argc, char **argv)
{
    struct vm *vm1 = malloc(sizeof(struct vm));
    struct vm *vm2 = malloc(sizeof(struct vm));

    kvm_init(vm1, vm2);

    if (kvm_create_vm(vm1, RAM_SIZE) < 0)
    {
        fprintf(stderr, "create vm fault\n");
        return -1;
    }

    if (kvm_create_vm(vm2, RAM_SIZE) < 0)
    {
        fprintf(stderr, "create vm fault\n");
        return -1;
    }

    load_binary(vm1, BINARY_FILE1);
    load_binary(vm2, BINARY_FILE2);

    vm1->vcpu_number = 1;
    vm1->vcpus = kvm_init_vcpu(vm1, 0, kvm_cpu_thread);

    vm2->vcpu_number = 1;
    vm2->vcpus = kvm_init_vcpu(vm2, 0, kvm_cpu_thread);

    kvm_run_vm(vm1, vm2);

    kvm_clean_vm(vm1);
    kvm_clean_vm(vm2);

    kvm_clean_vcpu(vm1->vcpus);
    kvm_clean_vcpu(vm2->vcpus);
    kvm_clean(vm1);
    kvm_clean(vm2);
}