#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_AUTHOR("Qubasa Corp.");
MODULE_LICENSE("GPL v2");

const unsigned int EFER_ADDR = 0xC0000080;
const unsigned int VM_CR_ADDR = 0xC0010114;

enum SVM_SUPPORT {
  SVM_ALLOWED,
  SVM_NOT_AVAIL,
  SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE,
  SVM_DISABLED_WITH_KEY
};

bool hasMsrSupport(void) {
  uint32_t cpuid_response;

  // Get CPUID for MSR support
  __asm__("mov rax, 0x00000001" ::: "rax");
  __asm__("cpuid");
  __asm__("mov %0, edx" : "=r"(cpuid_response));

  if (cpuid_response & (1 << 5)) {
    return true;
  }
  return false;
}

void readMSR(uint32_t id, uint32_t *hi, uint32_t *lo) {
  __asm__("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(id));
}

void writeMSR(uint32_t id, uint32_t hi, uint32_t lo) {
  printk(KERN_INFO "Trying to write to 0x%x\n", id);
  __asm__("wrmsr" : : "a"(lo), "d"(hi), "c"(id));
}

bool isSvmDisabled_VM_CR(void) {
  uint32_t vm_cr;
  uint32_t high;

  // Read VM_CR MSR
  readMSR(VM_CR_ADDR, &high, &vm_cr);

  printk(KERN_INFO "Is SVM Lock enabled: %s\n",
         vm_cr & (1 << 3) ? "true" : "false");

  return (bool)(vm_cr & (1 << 4));
}

enum SVM_SUPPORT checkSvmSupport(void) {
  uint32_t cpuid_response;

  // Get CPUID for svm support
  __asm__("mov rax, 0x80000001" ::: "rax");
  __asm__("cpuid");
  __asm__("mov %0, ecx" : "=r"(cpuid_response));

  // Has SVM extension?
  if (!(cpuid_response & 0x2)) {
    return SVM_NOT_AVAIL;
  }

  if (!isSvmDisabled_VM_CR()) {
    return SVM_ALLOWED;
  }

  // Get CPUID for disabled svm at bios
  __asm__("mov rax, 0x8000000A" ::: "rax");
  __asm__("cpuid");
  __asm__("mov %0, edx" : "=r"(cpuid_response));

  // Check if SVM is disabled in BIOS
  if ((cpuid_response & 0x2) == 0) {
    return SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE;
  } else {
    return SVM_DISABLED_WITH_KEY;
  }
}

void inline enableSVM_EFER(void) {
  uint32_t efer;
  uint32_t high;
  uint64_t cr0;
  uint64_t cs;

  // Check CPL0
  // Processor must be in protected mode

  // Read VM_CR MSR
  readMSR(EFER_ADDR, &high, &efer);
  printk("Read EFER_ADDR content: 0x%x\n", efer);

  __asm__("mov %0, cr0" : "=r"(cr0));
  printk(KERN_INFO "Is protected mode enabled: %s\n",
         cr0 & 1 ? "true" : "false");

  __asm__("mov %0, cs" : "=r"(cs));
  printk(KERN_INFO "DPL is: %lld\n", cs & ((1 << 13) | (1 << 14)));

  printk(KERN_INFO "Is EFER.SVM enabled: %s\n",
         efer & (1 << 12) ? "true" : "false");

  efer |= 1 << 12; // BUG: This creates the GP exception
  writeMSR(EFER_ADDR, high, efer);
  // If enabled should look like this 0x1d01
  printk(KERN_INFO "Write EFER_ADDR content: 0x%x\n", efer);
}

bool vmrun(void) {
  void *vmcb;

  // TODO: Check if memory is write back
  vmcb = (void *)kzalloc(4096, GFP_KERNEL);

  if (vmcb == NULL) {
    printk(KERN_INFO "Could not allocate memory for vmcb\n");
    return false;
  }

  // Check if vcmb is 4k aligned in memory
  if ((uint64_t)vmcb % 4096 != 0) {
    printk(KERN_INFO "VMCB is not 4k aligned!\n");
    return false;
  }

  enableSVM_EFER();

  kfree(vmcb);
  // Execute VMRUN instruction
  /* __asm__("mov rax, %0":"=r"(vmcb)); */
  /* __asm__("vmrun"); */

  return true;
}

static int my_init(void) {
  enum SVM_SUPPORT svm;
  printk(KERN_INFO "==== LOADED HYPERVISOR DRIVER ====\n");

  if (!hasMsrSupport()) {
    printk(KERN_INFO "System does not have MSR support\n");
    return 1;
  }

  svm = checkSvmSupport();

  switch (svm) {
  case SVM_ALLOWED:
    printk(KERN_INFO "Has SVM support: true\n");
    break;
  case SVM_NOT_AVAIL:
    printk(KERN_INFO "Has SVM support: false\n");
    return 1;
  case SVM_DISABLED_WITH_KEY:
    printk(KERN_INFO "SVM is bios disabled with key\n");
    return 1;
  case SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE:
    printk(KERN_INFO "SVM is bios disabled not unlockable\n");
    return 1;
  }

  if (!vmrun()) {
    printk(KERN_INFO "vmrun failed\n");
    return 1;
  }

  return 0;
}

static void my_exit(void) {
  printk(KERN_INFO "Goodbye world.\n");

  return;
}

module_init(my_init);
module_exit(my_exit);