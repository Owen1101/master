#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/ioport.h>	//resource_size_t
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <asm/pgtable.h>
#include <asm-generic/cacheflush.h>

#include "reg_fpga.h"

#define	PCI_DEVICE_ID_XILINX	0x7021
#define BAR0_BYTE_SIZE		(1024*1024)
#define BUF_SIZE			(4096*1024)

#define DMA_TYPE		'D'
#define DMA_START_CHAN0		_IO(DMA_TYPE,1)
#define DMA_START_CHAN1		_IO(DMA_TYPE,2)
#define DMA_START_CHAN2		_IO(DMA_TYPE,3)
#define DMA_START_CHAN3		_IO(DMA_TYPE,4)

struct pcie_private {
	struct pci_dev *pdev;
	void __iomem *pcie_bar0;
	void __iomem *pcie_bar1;
//	struct work_struct work;
};
static struct pcie_private *adapter;
resource_size_t bar0_addr,bar1_addr;
wait_queue_head_t readq; 

long read_buf ;
char *write_buf = NULL;
dma_addr_t read_phyaddr;
dma_addr_t write_phyaddr;
static int flag = 0;
static int int_disable = 0, int_mask = 0;

static ssize_t 
pcie_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
	int result;
	if(wait_event_interruptible(readq, flag != 0))//fpga_cs_status() == 1))
		return -ERESTARTSYS;
	flag = 0;

	result = copy_to_user(buf,(char *)read_buf,count);
	if(result < 0){
		printk("copy_to_user fail....\n");
		return -EFAULT;
	}
	return count;
}

static void init_fpga(void)
{
	int result;

	writel(0x1,adapter->pcie_bar0 + PCIEFC_SOFTRESET);
	udelay(100);
	writel(0x0,adapter->pcie_bar0 + PCIEFC_SOFTRESET);//复位接受缓冲区
	udelay(100);

	writel(read_phyaddr,adapter->pcie_bar0 + PCIEFC_DMARADDR_L);//主机读DMA
	writel(0x0,adapter->pcie_bar0 + PCIEFC_DMARADDR_U);

	writel(BUF_SIZE,adapter->pcie_bar0 + PCIEFC_DMARLEN_8);//主机读DMA长度

	writel(0x0,adapter->pcie_bar0 + PCIEFC_INTDISABLE);//使能DMA中断
	int_disable = readl(adapter->pcie_bar0 + PCIEFC_INTDISABLE);

	writel(0x0,adapter->pcie_bar0 + PCIEFC_INTMASK);
	int_mask = readl(adapter->pcie_bar0 + PCIEFC_INTMASK);

	result = readl(adapter->pcie_bar0 + PCIEFC_DMACST);
}

static long
pcie_ioctl(struct file *filp, unsigned int req, unsigned long arg)
{
	int res ;
	switch(req){
		case DMA_START_CHAN0:
			writel(0x0,adapter->pcie_bar0 + PCIEFC_CHANSEL_DMAWR);
			break;
		case DMA_START_CHAN1:
			writel(0x1,adapter->pcie_bar0 + PCIEFC_CHANSEL_DMAWR);
			break;
		case DMA_START_CHAN2:
			writel(0x2,adapter->pcie_bar0 + PCIEFC_CHANSEL_DMAWR);
			break;
		case DMA_START_CHAN3:
			writel(0x3,adapter->pcie_bar0 + PCIEFC_CHANSEL_DMAWR);
			break;
		default:
			printk("Cannot support ioctl %#x\n", req);
			return -1;
	}

	writel(0x1,adapter->pcie_bar0 + PCIEFC_DMACST);
	res = readl(adapter->pcie_bar0 + PCIEFC_DMACST);
	while((res & 0x2) == 0)
		res = readl(adapter->pcie_bar0 + PCIEFC_DMACST);
	writel(0x2,adapter->pcie_bar0 + PCIEFC_DMACST);

	res = readb(adapter->pcie_bar0 + PCIEFC_INTSTATE);
//	printk("res = %x\n",res);
	if((res & 0x10) && (~int_mask & 0x10)){
		flag = 1;
		wake_up_interruptible(&readq);
	}

	return 0;
}

static struct file_operations pcie_fops = {
	.owner     		= THIS_MODULE,
	.read			= pcie_read,
//	.mmap      		= pcie_mmap,
	.unlocked_ioctl	= pcie_ioctl,
};

static struct miscdevice fpga_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fpga-pcie",
	.fops = &pcie_fops,
};

static int
pcie_probe(struct pci_dev *pdev,const struct pci_device_id *id)
{
	int result;
	long bar0_length;
	
	adapter = kmalloc(sizeof(struct pcie_private),GFP_KERNEL);
	if(NULL == adapter){
		printk("kmalloc fail.\n");
		return -ENOMEM;
	}

	adapter->pdev = pdev;

	result = pci_enable_device(pdev);
	if (result) {
		printk("pci_enable_device() failed, result = %d.\n", result);
		goto alloc_free;
	}

	pci_set_master(pdev);

	result = pci_request_region(pdev,0,"fpga-pcie");//为对端bar0申请资源
	if(result <0){
		printk("Bar0 Request failed.\n");
		goto disable_dev;
	}
	else
		printk("Bar0 region request success.\n");

	bar0_addr = pci_resource_start(pdev, 0);//获取对端bar0起始地址
	if( bar0_addr < 0){
		result = -EIO;
		printk("Bar0 no MMIO.\n");
		goto region_free;
	}

	bar0_length = pci_resource_len(pdev,0);
	if(bar0_length < BAR0_BYTE_SIZE){
		result = -EIO;
		printk("MMIO is too small for bar0.\n");
		goto region_free;
	}

	adapter->pcie_bar0 = pci_iomap(pdev,0,bar0_length);
	if(NULL == adapter->pcie_bar0){
		result = -EIO;
		printk("Bar0 can't map MMIO.\n");
		goto region_free;
	}

	pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));

	init_waitqueue_head(&readq);

	result = misc_register(&fpga_misc);
	if(result < 0){
		printk("register failed\n");
		goto region_free;
	}

	read_buf = __get_free_pages(GFP_KERNEL|GFP_DMA ,6);
	if(read_buf < 0){
		printk("Unable to allocate rdbuf.");
		return -1;
	}

	init_fpga();

	printk("probe success\n");
	return 0;

region_free:
	pci_release_region(pdev,0);
disable_dev:
	pci_disable_device(pdev);
alloc_free:
	kfree(adapter);
	return result;
}

static void 
pcie_remove(struct pci_dev *pdev)
{
	pdev = adapter->pdev;

	if(read_buf != 0)
		free_pages(read_buf,6);
	read_buf = 0;

	misc_deregister(&fpga_misc);

	pci_iounmap(pdev,adapter->pcie_bar0);
	adapter->pcie_bar0 = NULL;

	pci_release_region(pdev,0);

	pci_disable_device(pdev);

	kfree(adapter);
	printk(KERN_NOTICE "kmalloc has been freed\n");
}

static struct pci_device_id fpga_pcie_id[] = { 

	{PCI_DEVICE(PCI_VENDOR_ID_XILINX,PCI_DEVICE_ID_XILINX),},
	{0,}
};

MODULE_DEVICE_TABLE(pcie,fpga_pcie_id);

static struct pci_driver fpga_pcie_driver = { 
    	.name 		= "fpga-pcie",
		.id_table 	= fpga_pcie_id,
		.probe 		= pcie_probe,
		.remove 	= pcie_remove,
};

static int __init my_init(void)
{
	int result;
	result = pci_register_driver(&fpga_pcie_driver);
	return result;
}

static void __exit my_exit(void)
{
	pci_unregister_driver(&fpga_pcie_driver);
}

module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");
