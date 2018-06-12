#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/ioport.h>	//resource_size_t
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
//#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
//#include <linux/scatterlist.h>
//#include <asm/pgtable.h>
#include <asm-generic/cacheflush.h>

#include "reg_fpga.h"

#define	PCI_DEVICE_ID_XILINX	0x7021
#define BAR0_BYTE_SIZE		(1024*1024)
//#define BUF_SIZE			(32*1024*1024)
#define BUF_SIZE			(8*1024*1024)
//#define BUF_SIZE			(4*1024*1024)

#define DMA_TYPE		'D'
#define DMA_START_CHAN0		_IO(DMA_TYPE,1)
#define	DMA_CHAN_PARM		_IO(DMA_TYPE,2)
#define DMA_STOP_CHAN0     	_IO(DMA_TYPE,3)
#define DMA_DISABLE_AD		_IO(DMA_TYPE,4)

struct pcie_private {
	struct pci_dev *pdev;
	void __iomem *pcie_bar0;
	void __iomem *pcie_bar1;
//	struct work_struct work;
};
static struct pcie_private *adapter;
resource_size_t bar0_addr,bar1_addr;
wait_queue_head_t readq; 

char *read_buf = NULL;
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

	dma_sync_single_for_cpu(&adapter->pdev->dev,read_phyaddr,
							BUF_SIZE,PCI_DMA_FROMDEVICE);//刷新cache
	result = copy_to_user(buf,(char *)read_buf,count);
//	memset(read_buf,'\0',count);
	if(result < 0){
		printk("copy_to_user fail....\n");
		return -EFAULT;
	}
	return count;
}

static void init_fpga(void)
{
	//int result;

	writel(0x1,adapter->pcie_bar0 + PCIEFC_SOFTRESET);
	udelay(100);
	writel(0x0,adapter->pcie_bar0 + PCIEFC_SOFTRESET);//复位接受缓冲区
	udelay(100);

	read_phyaddr = virt_to_phys(read_buf);
	printk("read_phyaddr = %x\n",read_phyaddr);
	writel(read_phyaddr,adapter->pcie_bar0 + PCIEFC_DMARADDR_L);//主机读DMA
	writel(0x0,adapter->pcie_bar0 + PCIEFC_DMARADDR_U);

	writel(BUF_SIZE,adapter->pcie_bar0 + PCIEFC_DMARLEN_8);//主机读DMA长度

	writel(0x0,adapter->pcie_bar0 + PCIEFC_INTDISABLE);//使能DMA中断
	int_disable = readl(adapter->pcie_bar0 + PCIEFC_INTDISABLE);

	writel(0x0,adapter->pcie_bar0 + PCIEFC_INTMASK);
	int_mask = readl(adapter->pcie_bar0 + PCIEFC_INTMASK);

	writel(0x0,adapter->pcie_bar0 + DELAY_TIME);//AD采集使能之前的延迟时间
	udelay(200);

//	writel(0x4c4b400/*0x2625a00*/,adapter->pcie_bar0 + SAMPLE_TIME);
	writel(0x3/*0x2625a00*/,adapter->pcie_bar0 + SAMPLE_TIME);
	udelay(200);//AD采样时间(1s*80M=0x4c4b400)

//	writel(0x4c4b400,adapter->pcie_bar0 + TRIGGER_CNT_EXTERN);
	writel(0x3/*0x2625a00*/,adapter->pcie_bar0 + TRIGGER_CNT_EXTERN);
	udelay(200);//触发时间总计数(1s*80M=0x4c4b400)

	writel(0x0,adapter->pcie_bar0 + TRIGGER_OPTION);
	udelay(200);//触发选择(0内触发/1外触发)

	writel(0x0,adapter->pcie_bar0 + AD_TEST_SEL);
	udelay(200);//数据源数据选择(0累加测试数据/1AD数据)

	writel(0x0,adapter->pcie_bar0 + TRIGGER_EN);
	udelay(200);
	writel(0x1,adapter->pcie_bar0 + TRIGGER_EN);//AD触发使能(1使能)
	udelay(200);
	writel(0x40,adapter->pcie_bar0 + WORK_MODE);//工作模式

}

static long
pcie_ioctl(struct file *filp, unsigned int req, unsigned long arg)
{
	int res = 0,value = 0;
//	int i = 0,j = 0;
	size_t size = 8;
	const void *__user rcv_buf = (void __user *)arg;
	char ker_buf[8] = {0};
	int ret = 0;
	switch(req){
		case DMA_START_CHAN0://通道1
			while(ret == 0){
				ret = readl(adapter->pcie_bar0 + PCIEFC_INTSTATE) & (1 << 0);//读DMA完成中断
	//			printk("ret = %08x\n",ret);
	//			i = readl(adapter->pcie_bar0 + PCIEFC_INTSTATE);
	//			printk("i = %08x ,j = %d\n",i,j++);
			}

			writel(0x0,adapter->pcie_bar0 + PCIEFC_CHANSEL_DMAWR);
			writel(0x1,adapter->pcie_bar0 + PCIEFC_DMACST);//启动DMA
			res = readl(adapter->pcie_bar0 + PCIEFC_DMACST);
			while((res & 0x2) == 0){
				msleep(5);
				res = readl(adapter->pcie_bar0 + PCIEFC_DMACST);//等待DMA完成
//			printk("-=+=-=+=\n");
//				printk("i = %d\n",i++);
			}//清除dma完成标志会出现隔十包左右完成一次dma需要3~4s
//			会使整体速度下降,改成检测中断标志位,FPGA在数据满时会置1
	//		writel(0x2,adapter->pcie_bar0 + PCIEFC_DMACST);//清除DMA完成标志位
			flag = 1;
			wake_up_interruptible(&readq);
			break;

		case DMA_CHAN_PARM:
			res = copy_from_user(ker_buf,rcv_buf,size);
			if(res < 0){
				printk("transfer parameters failed.\n");
				return -1;
			}
		//	printk("ker_buf = %x %x %x %x\n",ker_buf[0],ker_buf[1],ker_buf[2],ker_buf[3]);
			kfree(read_buf);
			read_buf = NULL;
			read_buf = kmalloc(BUF_SIZE,GFP_KERNEL);//申请DMA空间
			if(NULL == read_buf){
				printk("Unable to allocate rdbuf.");
			return -1;
			}

			init_fpga();//初始化DMA及AD寄存器
		//	memset(read_buf,'\0',BUF_SIZE);

			writel(ker_buf[0],adapter->pcie_bar0 + TRAN_GAIN);
			writel(ker_buf[2],adapter->pcie_bar0 + SAMP_FREQ);
			value = (ker_buf[7] << 24) + (ker_buf[6] << 16) + 
						(ker_buf[5] << 8) + ker_buf[4];
			writel(value,adapter->pcie_bar0 + INTER_FREQ);

			writel(0x1,adapter->pcie_bar0 + AD_ENABLE);

			/*...*/
			break;

		case DMA_STOP_CHAN0:
			writel(0x0,adapter->pcie_bar0 + PCIEFC_CHANSEL_DMAWR);
			writel(0x0,adapter->pcie_bar0 + PCIEFC_DMACST);
			break;

		case DMA_DISABLE_AD:
			writel(0x0,adapter->pcie_bar0 + AD_ENABLE);
			break;

		default:
			printk("Cannot support ioctl %#x\n", req);
			return -1;
	}


	return 0;
}

static struct file_operations pcie_fops = {
	.owner     		= THIS_MODULE,
	.read			= pcie_read,
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

	result = pci_enable_device(pdev);//使能设备
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
//	else
//		printk("Bar0 region request success.\n");

	bar0_addr = pci_resource_start(pdev, 0);//获取对端bar0起始地址
	if( bar0_addr < 0){
		result = -EIO;
		printk("Bar0 no MMIO.\n");
		goto region_free;
	}

	bar0_length = pci_resource_len(pdev,0);//获取bar0长度
	if(bar0_length < BAR0_BYTE_SIZE){
		result = -EIO;
		printk("MMIO is too small for bar0.\n");
		goto region_free;
	}

	adapter->pcie_bar0 = pci_iomap(pdev,0,bar0_length);//映射bar0
	if(NULL == adapter->pcie_bar0){
		result = -EIO;
		printk("Bar0 can't map MMIO.\n");
		goto region_free;
	}

	pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));

	init_waitqueue_head(&readq);//工作队列初始化

	result = misc_register(&fpga_misc);//注册miscdev
	if(result < 0){
		printk("register failed\n");
		goto region_free;
	}

//	read_buf = kmalloc(BUF_SIZE,GFP_KERNEL);//申请DMA空间
	read_buf = dma_alloc_coherent(&adapter->pdev->dev,BUF_SIZE,read_phyaddr,GFP_KERNEL);
	if(NULL == read_buf){
		printk("Unable to allocate rdbuf.");
		return -1;
	}
	
//	printk("read = %p\n",read_buf);

	init_fpga();//初始化DMA及AD寄存器

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

	if(read_buf != NULL)
//		kfree(read_buf);
		dma_free_coherent(&adapter->pdev->dev,BUF_SIZE,read_buf,read_phyaddr);
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
