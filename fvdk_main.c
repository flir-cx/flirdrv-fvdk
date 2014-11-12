/***********************************************************************
 *                                                                     
 * Project: Balthazar
 * $Date$
 * $Author$
 *
 * $Id$
 *
 * Description of file:
 *    FLIR Video Device driver.
 *    Main/Ioctl functions
 *
 * Last check-in changelist:
 * $Change$
 * 
 *
 * Copyright: FLIR Systems AB.  All rights reserved.
 *
 ***********************************************************************/


#include "flir_kernel_os.h"
#include "fpga.h"
#include "fvdkernel.h"
#include "fvdk_internal.h"
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include "../arch/arm/mach-imx/hardware.h"

#ifndef __devexit
#define __devexit
#endif

#else  // LINUX_VERSION_CODE
#include "mach/mx6.h"
    #define cpu_is_imx6s   cpu_is_mx6dl
    #define cpu_is_imx6q   cpu_is_mx6q
#endif

// Definitions

// Local prototypes
static long FVD_IOControl(struct file *filep,
                          unsigned int cmd,
                          unsigned long arg);
static int FVD_Open(struct inode *inode,
                    struct file *filp);
static DWORD DoIOControl(PFVD_DEV_INFO pDev,
                         DWORD  Ioctl,
                         PUCHAR pBuf,
                         PUCHAR pUserBuf);
static int FVD_mmap (struct file * filep,
                     struct vm_area_struct * vma);

// Local variables
static PFVD_DEV_INFO gpDev;

static char * gpBlob;
static int blobsize;

// Code


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static int read_proc(struct seq_file *m, void *v);
static int fvd_proc_open(struct inode *inode, struct file *file);

static const struct file_operations fvd_proc_fops = {
     .owner = THIS_MODULE,
     .open		= fvd_proc_open,
    .read		= seq_read,
    .llseek		= seq_lseek,
    .release	= single_release,

};

static int fvd_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, read_proc, PDE_DATA(inode));
}


static int
read_proc(struct seq_file *m, void *v)
{

   PFVD_DEV_INFO pdev = (PFVD_DEV_INFO) m->private;
   seq_printf(m,
           "FPGA file    : %s\n"
           "Device mutex : %d (%d fails)\n"
           "Lepton mutex : %d (%d fails)\n"
           "Exec mutex   : %d (%d fails)\n",
           pdev->filename,
           pdev->iCtrMuDevice, pdev->iFailMuDevice,
           pdev->iCtrMuLepton, pdev->iFailMuLepton,
           pdev->iCtrMuExecute, pdev->iFailMuExecute);

    return 0;
}


#else

// Put data into the proc fs file.
static int FVD_procfs_read(char *page, char **start, off_t offset,
                               int page_size, int *eof, void *data)
{
    PFVD_DEV_INFO pdev = (PFVD_DEV_INFO) data;
    int len;

    if (offset > 0)
    {
        *eof = 1;
        return 0;
    }

    len = snprintf(page, page_size,
            "FPGA file    : %s\n"
            "Device mutex : %d (%d fails)\n"
            "Lepton mutex : %d (%d fails)\n"
            "Exec mutex   : %d (%d fails)\n",
            pdev->filename,
            pdev->iCtrMuDevice, pdev->iFailMuDevice,
            pdev->iCtrMuLepton, pdev->iFailMuLepton,
            pdev->iCtrMuExecute, pdev->iFailMuExecute);

    *eof = 1;
    return len;
}
#endif


static struct file_operations fvd_fops =
{
        .owner = THIS_MODULE,
        .unlocked_ioctl = FVD_IOControl,
        .open = FVD_Open,
        .mmap = FVD_mmap,
};



static int FVD_mmap (struct file * filep, struct vm_area_struct * vma)
{
    int size;

    size = vma->vm_end - vma->vm_start;

    if ( size > blobsize)
        return -EINVAL;
    if ( remap_pfn_range (vma, vma->vm_start, __pa(gpBlob) >> PAGE_SHIFT, size,
                  vma->vm_page_prot ) )
        return -EAGAIN ;
    return 0;
}




static int __init FVD_Init(void)
{
    int i;

    pr_err("FVD_Init\n");

    // Check that we are not already initiated
    if (gpDev) {
    	pr_err("FVD already initialized\n");
        return 0;
    }

    // Allocate (and zero-initiate) our control structure.
    gpDev = (PFVD_DEV_INFO)kmalloc(sizeof(FVD_DEV_INFO), GFP_KERNEL);
    if ( !gpDev ) {
    	pr_err("Error allocating memory for pDev, FVD_Init failed\n");
        return -2;
    }

    // Reset all data
    memset (gpDev, 0, sizeof(*gpDev));

    // Register linux driver
    alloc_chrdev_region(&gpDev->fvd_dev, 0, 1, "fvdk");
    cdev_init(&gpDev->fvd_cdev, &fvd_fops);
    gpDev->fvd_cdev.owner = THIS_MODULE;
    gpDev->fvd_cdev.ops = &fvd_fops;
    i = cdev_add(&gpDev->fvd_cdev, gpDev->fvd_dev, 1);
    if (i)
    {
    	pr_err("Error adding device driver\n");
        return -3;
    }
    gpDev->pLinuxDevice = platform_device_alloc("fvdk", 1);
    if (gpDev->pLinuxDevice == NULL)
    {
    	pr_err("Error adding allocating device\n");
        return -4;
    }
    if (cpu_is_mx51())
    	SetupMX51(gpDev);
    else if(cpu_is_imx6s())
    	SetupMX6S(gpDev);
    else if(cpu_is_imx6q())
        SetupMX6Q(gpDev);
    else
       {pr_err("FVD: Error: Unkown Hardware\n");return -4;}


    platform_device_add(gpDev->pLinuxDevice);
	pr_err("FVD driver device id %d.%d added\n", MAJOR(gpDev->fvd_dev), MINOR(gpDev->fvd_dev));
    gpDev->fvd_class = class_create(THIS_MODULE, "fvdk");
    device_create(gpDev->fvd_class, NULL, gpDev->fvd_dev, NULL, "fvdk");

    // DDK not used as DLL to avoid compatibility issues between fvd.dll and OS image
    if (!gpDev->pSetupGpioAccess())
    {
        kfree(gpDev);
    	pr_err("Error setting up GPIO\n");
        return -5;
    }

    /* Setup /proc read only file system entry. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    gpDev->proc = proc_create_data("fvdk", 0, NULL, &fvd_proc_fops, gpDev);
#else
    gpDev->proc = create_proc_read_entry("fvdk", 0, NULL, FVD_procfs_read, gpDev);
#endif



    if (gpDev->proc == NULL) {
        pr_err("failed to add proc fs entry\n");
    }

    sema_init(&gpDev->muDevice, 1);
    sema_init(&gpDev->muLepton, 1);
    sema_init(&gpDev->muExecute, 1);

	pr_err("FVD_Init completed\n");

    return 0;
}

static void __devexit FVD_Deinit(void)
{
    pr_err("FVD_Deinit\n");

    // make sure this is a valid context
    // if the device is running, stop it
    if (gpDev != NULL)
    {
        gpDev->pBSPFvdPowerDown(gpDev);
        gpDev->pCleanupGpio(gpDev);
        device_destroy(gpDev->fvd_class, gpDev->fvd_dev);
        class_destroy(gpDev->fvd_class);
        unregister_chrdev_region(gpDev->fvd_dev, 1);
    	platform_device_unregister(gpDev->pLinuxDevice);
        kfree(gpDev);
        gpDev = NULL;
        remove_proc_entry("fvdk",NULL);
        if (gpBlob)
        {
            kfree(gpBlob);
            gpBlob = NULL;
        }
    }
}

////////////////////////////////////////////////////////
//
// FVD_Open
//
////////////////////////////////////////////////////////
static int FVD_Open (struct inode *inode, struct file *filp)
{

    static BOOL init;
    DWORD dwStatus;
    DWORD timeout = 50;

    down(&gpDev->muDevice);

    if (!init && gpDev->pGetPinDone())
    {
        pr_debug ("FVD_Open: Fpga already loaded in uboot");
        init = TRUE;
    }
    else if (!init)
    {
        gpDev->pBSPFvdPowerUp(gpDev);

        pr_err("FVD will load FPGA\n");

        // Load MAIN FPGA
        dwStatus = LoadFPGA(gpDev, "");

        if (dwStatus != ERROR_SUCCESS)
        {
            pr_debug ("FVD_Init: LoadFPGA failed %lu\n", dwStatus);
            return -1;
        }

        // Wait until FPGA loaded
        while (timeout--)
        {
            msleep (10);
            if (gpDev->pGetPinReady() == 0)
                break;
        }
        init = TRUE;
    }
    up(&gpDev->muDevice);

    return 0;
}

////////////////////////////////////////////////////////
//
// FVD_IOControl
//
////////////////////////////////////////////////////////
static long FVD_IOControl(struct file *filep,
		unsigned int cmd, unsigned long arg)
{
    DWORD dwErr = ERROR_SUCCESS;
    char *tmp;

    tmp = kzalloc(_IOC_SIZE(cmd), GFP_KERNEL);
    if (_IOC_DIR(cmd) & _IOC_WRITE)
    {
//		pr_err("Ioctl %X copy from user: %ld\n", cmd, _IOC_SIZE(cmd));
    	dwErr = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
    	if (dwErr)
    		pr_err("FVD: Copy from user failed: %ld\n", dwErr);
    }

    if (dwErr == ERROR_SUCCESS)
    {
//		pr_err("Ioctl %X\n", cmd);
    	dwErr = DoIOControl(gpDev, cmd, tmp, (PUCHAR)arg);
    	if (dwErr)
    		pr_err("FVD Ioctl %X failed: %ld\n", cmd, dwErr);
    }

    if ((dwErr == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ))
    {
//		pr_err("Ioctl %X copy to user: %ld\n", cmd, _IOC_SIZE(cmd));
    	dwErr = copy_to_user((void *)arg, tmp, _IOC_SIZE(cmd));
    	if (dwErr)
    		pr_err("FVD: Copy to user failed: %ld\n", dwErr);
    }
    kfree(tmp);

    return dwErr;
}


////////////////////////////////////////////////////////
//
// DoIOControl
//
////////////////////////////////////////////////////////
DWORD DoIOControl(
                  PFVD_DEV_INFO pDev,
                  DWORD  Ioctl,
                  PUCHAR pBuf,
                  PUCHAR pUserBuf)
{
    DWORD  dwErr = ERROR_INVALID_PARAMETER;

    switch (Ioctl)
    {
    case IOCTL_FVDK_GET_VERSION:    // return driver version
        *(ULONG*)pBuf = FVD_VERSION;
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_POWER_UP:
        pDev->pBSPFvdPowerUp(pDev);
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_POWER_DOWN:
        pDev->pBSPFvdPowerDown(pDev);
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_POWER_UP_FPA:
        pDev->pBSPFvdPowerUpFPA(pDev);
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_POWER_DOWN_FPA:
        pDev->pBSPFvdPowerDownFPA(pDev);
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_GET_FPGA_GENERIC:
        memcpy(pBuf, &pDev->fpga[0], sizeof(GENERIC_FPGA_T));
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_GET_FPGA_DATA:
        memcpy(pBuf, &pDev->fpga[sizeof(GENERIC_FPGA_T)], sizeof(BXAB_FPGA_T));
        dwErr = ERROR_SUCCESS;
        break;

    case IOCTL_FVDK_GET_FPGA_BUF:
        {
            BXAB_FPGA_T * pSpec = (BXAB_FPGA_T *)&pDev->fpga[sizeof(GENERIC_FPGA_T)];
            dwErr = copy_to_user(pUserBuf, &pDev->fpga[sizeof(GENERIC_FPGA_T) + sizeof(BXAB_FPGA_T)],
                pSpec->noOfBuffers * sizeof(SDRAM_BUF_T));
        }
        break;

    case IOCTL_FVDK_CREATE_BLOB:
        if (gpBlob)
            dwErr = ERROR_SUCCESS;
        else
        {
            blobsize = (*(ULONG*)pBuf + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            gpBlob = kmalloc (blobsize, GFP_KERNEL) ;
            if (gpBlob == NULL)
            {
                pr_err ( "FVDK : Error allocating memory\n") ;
                blobsize = 0;
                dwErr = -ENOMEM;
            }
            else
            {
                memset(gpBlob, 0, blobsize);
                dwErr = ERROR_SUCCESS;
            }
        }
        break;

    case IOCTL_FVDK_LOCK:
        {
            int lock = *(ULONG*)pBuf >> 16;
            int unlock = *(ULONG*)pBuf & 0xFFFF;

            switch (unlock)
            {
            case LDRV:
                up(&gpDev->muDevice);
                break;
            case LEXEC:
                up(&gpDev->muExecute);
                break;
            case LLEPT:
                up(&gpDev->muLepton);
                break;
            }
            switch (lock)
            {
            case LNONE:
                dwErr = ERROR_SUCCESS;
                break;
            case LDRV:
                dwErr = down_timeout(&gpDev->muDevice, msecs_to_jiffies(2000));
                pDev->iCtrMuDevice++;
                if (dwErr)
                    pDev->iFailMuDevice++;
                break;
            case LEXEC:
                dwErr = down_timeout(&gpDev->muExecute, msecs_to_jiffies(2000));
                pDev->iCtrMuLepton++;
                if (dwErr)
                    pDev->iFailMuLepton++;
                break;
            case LLEPT:
                dwErr = down_timeout(&gpDev->muLepton, msecs_to_jiffies(2000));
                pDev->iCtrMuExecute++;
                if (dwErr)
                    pDev->iFailMuExecute++;
                break;
            }
            if (dwErr)
                pr_err("Lock failed %d %d %d\n", unlock, lock, (int)dwErr);
        }
        break;

    default:
        pr_err("FVDK: Ioctl %lX not supported\n", Ioctl);
        dwErr = ERROR_NOT_SUPPORTED;
        break;
    }

    return dwErr;
}

module_init (FVD_Init);
module_exit (FVD_Deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Video Driver");
MODULE_AUTHOR("Peter Fitger");

//******************************************************************

