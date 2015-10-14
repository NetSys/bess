//TODO:COPYRIGHT stuff

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>

#include "sn.h"
#include "sn_ivshmem.h"

#define DEVICE_NAME "sn0"

#define INTRMASK_OFFSET (0x00)
#define INTRSTAT_OFFSET (0x04)
#define VMID_OFFSET (0x08)
#define DOORBELL_OFFSET (0x0c)

#define MSI_VECTOR_SIZE 16

static wait_queue_head_t wait_queue;
DEFINE_MUTEX(sn_ivsm_mutex);

/* pci driver ops */
static int sn_ivsm_probe_device (struct pci_dev *pdev,
				    const struct pci_device_id * ent);
static void sn_ivsm_remove_device(struct pci_dev* pdev);
static irqreturn_t sn_ivsm_interrupt (int irq, void *dev_instance);

/* internal functions */
static int sn_ivsm_unregister_interrupt(void);
static int sn_ivsm_request_msix_vectors(unsigned int nvec);

static struct pci_device_id sn_ivsm_id_table[] = {
	{ 0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 },
};
MODULE_DEVICE_TABLE (pci, sn_ivsm_id_table);

static struct pci_driver sn_ivsm_pci_driver = {
	.name = DEVICE_NAME,
	.id_table = sn_ivsm_id_table,
	.probe = sn_ivsm_probe_device,
	.remove = sn_ivsm_remove_device,
};

typedef struct sn_ivsm_device {
	void __iomem * regs;
	void * base_addr;

	unsigned long regaddr;
	unsigned long reg_size;
	
	unsigned long ioaddr;
	unsigned long ioaddr_size;
	unsigned int irq;

	struct pci_dev *dev;
	char (*msix_names)[256];
	struct msix_entry *msix_entries;
	int nvectors;
	bool msix_enabled;
	void (*interrupt_handler)(int irq, u32 msg);
} sn_ivsm_device;

sn_ivsm_device sn_ivsm_dev;

static int sn_ivsm_request_msix_vectors(unsigned int nvec)
{
	int i, ret, cpu;
	
	sn_ivsm_dev.nvectors = nvec;
	sn_ivsm_dev.msix_entries = kmalloc(nvec * sizeof(*sn_ivsm_dev.msix_entries),
					GFP_KERNEL);

	if (!sn_ivsm_dev.msix_entries) {
		return -ENOMEM;
	}

	sn_ivsm_dev.msix_names = kmalloc(nvec * sizeof(*sn_ivsm_dev.msix_names),
				      GFP_KERNEL);

	if (!sn_ivsm_dev.msix_names) {
		kfree(sn_ivsm_dev.msix_entries);
		sn_ivsm_dev.msix_entries = NULL;
		return -ENOMEM;
	}


	for (i = 0; i < nvec; i++)
		sn_ivsm_dev.msix_entries[i].entry = i;

	ret = pci_enable_msix(sn_ivsm_dev.dev, sn_ivsm_dev.msix_entries,
			      sn_ivsm_dev.nvectors);
	if (0 != ret) {
		log_info("no MSI pci_enable_msix ret: %d\n", ret);
		return -ENOSPC;
	}

	
	for (i = 0, cpu = cpumask_first(cpu_online_mask); 
	     i < nvec;
	     i++, cpu = cpumask_next(cpu, cpu_online_mask) ) {
		//cpumask_t cpumask;
		//int irq = sn_ivsm_dev.msix_entries[i].vector;

		snprintf(sn_ivsm_dev.msix_names[i], sizeof(*sn_ivsm_dev.msix_names),
			 "%s-%d", DEVICE_NAME, i);

		ret = request_irq(sn_ivsm_dev.msix_entries[i].vector,
				  &sn_ivsm_interrupt, 0,
				  sn_ivsm_dev.msix_names[i], &sn_ivsm_dev);

		if (0 != ret) {
			log_err("couldn't allocate irq for msi-x ");
			log_err("entry %d with vector %d\n", 
				i, sn_ivsm_dev.msix_entries[i].vector);
			return -ENOSPC;
		}

		//map irq to the core. cannot be done due to unexported symbol irq_set_affinity
		//leaving the code for the future :)
		//
		//cpumask_clear(&cpumask);
		//cpumask_set_cpu(cpu, &cpumask);
		//log_info("setting irq %d affinity with cpu %d\n", irq, cpu);
		//	 ret = irq_set_affinity(irq, &cpumask);

	}

	log_info("MSI-X enabled\n");
	sn_ivsm_dev.msix_enabled = true;
	return 0;
}


static irqreturn_t sn_ivsm_interrupt(int irq, void *pdev_)
{
	u32 msg = 0;
	struct sn_ivsm_device *pdev = pdev_;

	if (unlikely(pdev == NULL))
		return IRQ_NONE;

	/* msg = readl(pdev->regs + INTRSTAT_OFFSET); */
	//if (!msg || (msg == 0xFFFFFFFF))
	//	return IRQ_NONE;

	if (sn_ivsm_dev.interrupt_handler) {
		//log_info("calling interrupt handler %p \n",pdev_);
		sn_ivsm_dev.interrupt_handler(irq, msg);
	}


	return IRQ_HANDLED;
}

int sn_ivsm_register_interrupt(unsigned int nvec)
{
	int ret;

	//try MSI-X
	ret = sn_ivsm_request_msix_vectors(nvec);

	/* if it doesn't work try with regular IRQ */
	if (ret) {
		log_info("MSI-X failed. USE Regular IRQs\n");
		if (request_irq(sn_ivsm_dev.dev->irq, &sn_ivsm_interrupt, IRQF_SHARED,
				DEVICE_NAME, &sn_ivsm_dev)) {
			log_err("register irq failed ");
			log_err("irq = %u regaddr = %lx reg_size = %ld\n",
				sn_ivsm_dev.dev->irq, sn_ivsm_dev.regaddr, 
				sn_ivsm_dev.reg_size);
			return -ENOSPC;
		}
	} 

	return 0;
}

static int sn_ivsm_unregister_interrupt(void)
{
	int i;
	if (sn_ivsm_dev.msix_enabled) {
		for (i = 0; i < sn_ivsm_dev.nvectors; i++) {
			free_irq(sn_ivsm_dev.msix_entries[i].vector, &sn_ivsm_dev);
		}
		pci_disable_msix(sn_ivsm_dev.dev);

		//free msix kmallocs
		if (NULL != sn_ivsm_dev.msix_entries) {
			kfree(sn_ivsm_dev.msix_entries);
			sn_ivsm_dev.msix_entries = NULL;
		}
		if (NULL != sn_ivsm_dev.msix_names) {
			kfree(sn_ivsm_dev.msix_names);
			sn_ivsm_dev.msix_names = NULL;
		}
	} else {
		free_irq(sn_ivsm_dev.dev->irq, &sn_ivsm_dev);
	}
	return 0;
}

static int sn_ivsm_probe_device (struct pci_dev *pdev,
				const struct pci_device_id * ent) {

	int ret;

	log_info("Probing for IVSHMEM Device\n");

	ret = pci_enable_device(pdev);
	if (0 != ret) {
		log_err("cannot probe SN_IVSMHMEM device %s: error %d\n",
		pci_name(pdev), ret);
		return ret;
	}

	ret = pci_request_regions(pdev, "sn_ivsmhmem");
	if (0 != ret) {
		log_err("pci request regions failed\n");
		goto pci_disable;
	}

	sn_ivsm_dev.ioaddr = pci_resource_start(pdev, 2);
	sn_ivsm_dev.ioaddr_size = pci_resource_len(pdev, 2);
	sn_ivsm_dev.base_addr = pci_iomap(pdev, 2, 0);

	log_info("shared memory base = 0x%lu, ioaddr = %lx io_addr_size = %ld\n",
		  (unsigned long) sn_ivsm_dev.base_addr,
		  sn_ivsm_dev.ioaddr,
		  sn_ivsm_dev.ioaddr_size);

	if (!sn_ivsm_dev.base_addr) {
		log_err("iomap region of size %ld failed\n",
			sn_ivsm_dev.ioaddr_size);
		goto pci_release;
	}

	sn_ivsm_dev.regaddr =  pci_resource_start(pdev, 0);
	sn_ivsm_dev.reg_size = pci_resource_len(pdev, 0);
	sn_ivsm_dev.regs = pci_iomap(pdev, 0, 0x100);

	log_info("pci register addr = 0x%p, regaddr = %lx reg_size = %ld\n",
		  sn_ivsm_dev.regs,
		  sn_ivsm_dev.regaddr,
		  sn_ivsm_dev.reg_size);

	sn_ivsm_dev.dev = pdev;

	if (!sn_ivsm_dev.regs) {
		log_err("iomap registers of size %ld failed\n",
		       sn_ivsm_dev.reg_size);
		goto reg_release;
	}

	/* set all masks to on */
	writel(0xffffffff, sn_ivsm_dev.regs + INTRMASK_OFFSET);

	init_waitqueue_head(&wait_queue);

	return 0;
 reg_release:
	pci_iounmap(pdev, sn_ivsm_dev.base_addr);	
 pci_release:
	pci_release_regions(pdev);
 pci_disable:
	pci_disable_device(pdev);
	return -EBUSY;

}

static void sn_ivsm_remove_device(struct pci_dev* pdev)
{

	log_info("remove sn_ivsmhmem device.\n");
	sn_ivsm_unregister_interrupt();
	pci_iounmap(pdev, sn_ivsm_dev.regs);
	pci_iounmap(pdev, sn_ivsm_dev.base_addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

long sn_ivsm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long len;
	unsigned long off;
	unsigned long start;

	mutex_lock(&sn_ivsm_mutex);

	off = vma->vm_pgoff << PAGE_SHIFT;
	start = sn_ivsm_dev.ioaddr;

	len=PAGE_ALIGN((start & ~PAGE_MASK) + sn_ivsm_dev.ioaddr_size);
	start &= PAGE_MASK;

	printk(KERN_INFO "%lu - %lu + %lu\n",vma->vm_end ,vma->vm_start, off);
	printk(KERN_INFO "%lu > %lu\n",(vma->vm_end - vma->vm_start + off), len);

	if ((vma->vm_end - vma->vm_start + off) > len) {
		mutex_unlock(&sn_ivsm_mutex);
		return -EINVAL;
	}

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	vma->vm_flags |= VM_SHARED|VM_DONTEXPAND | VM_DONTDUMP;

	if(io_remap_pfn_range(vma, vma->vm_start,
		off >> PAGE_SHIFT, vma->vm_end - vma->vm_start,
		vma->vm_page_prot))
	{
		printk("mmap failed\n");
		mutex_unlock(&sn_ivsm_mutex);
		return -ENXIO;
	}
	mutex_unlock(&sn_ivsm_mutex);

	return 0;
}

void sn_ivsm_register_ih(void (*ih)(int itr, u32 msg))
{
	log_info("set interrupt handler\n");
	sn_ivsm_dev.interrupt_handler = ih;
}

void *sn_ivsm_get_start(void)
{
	return sn_ivsm_dev.base_addr;
}

long sn_ivsm_get_len(void)
{
	return sn_ivsm_dev.ioaddr_size;
}

int sn_ivsm_irq_to_qid(int irq)
{
	int irq_base = sn_ivsm_dev.msix_entries[0].vector;
	while (sn_ivsm_dev.msix_entries[irq - irq_base].vector != irq &&
	       (irq - irq_base) < (sn_ivsm_dev.nvectors - 1)) {
		irq_base--;
	} 
	return irq - irq_base;
}

int sn_ivsm_init(void)
{
	int ret = -ENOMEM;

	memset(&sn_ivsm_dev, 0, sizeof(sn_ivsm_dev));

	ret = pci_register_driver(&sn_ivsm_pci_driver);
	if (0 < ret) {
		log_err("pci_register_driver failed (%d)\n", ret);
		return ret;
	}
	return 0;
}

void sn_ivsm_cleanup(void)
{
	pci_unregister_driver(&sn_ivsm_pci_driver);
}


