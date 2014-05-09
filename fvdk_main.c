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

// Definitions

// Local prototypes
static long FVD_IOControl(struct file *filep,
                          unsigned int cmd,
                          unsigned long arg);
static DWORD DoIOControl(PFVD_DEV_INFO pDev,
                         DWORD  Ioctl,
                         PUCHAR pBuf,
                         PUCHAR pUserBuf);

// Local variables
static PFVD_DEV_INFO gpDev;

static struct file_operations fvd_fops =
{
        .owner = THIS_MODULE,
        .unlocked_ioctl = FVD_IOControl,
};

// Code

static int __init FVD_Init(void)
{
    DWORD dwStatus;
    int i;
    DWORD timeout = 50;

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
    else
    	SetupMX6S(gpDev);

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

    gpDev->giDevices++;

	pr_err("FVD_Init completed (%ld)\n", timeout);

    return 0;
}

static void __devexit FVD_Deinit(void)
{
    pr_err("FVD_Deinit\n");

    // make sure this is a valid context
    // if the device is running, stop it
    if (gpDev != NULL)
    {
        gpDev->giDevices--;
        gpDev->pBSPFvdPowerDown(gpDev);
        gpDev->pCleanupGpio(gpDev);
        device_destroy(gpDev->fvd_class, gpDev->fvd_dev);
        class_destroy(gpDev->fvd_class);
        unregister_chrdev_region(gpDev->fvd_dev, 1);
    	platform_device_unregister(gpDev->pLinuxDevice);
        if (gpDev->giDevices == 0)
        {
        	kfree(gpDev);
			gpDev = NULL;
        }
    }
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

    default:
        pr_err("Ioctl %lX not supported\n", Ioctl);
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

