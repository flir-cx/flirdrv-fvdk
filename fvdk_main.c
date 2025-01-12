/* SPDX-License-Identifier: GPL-2.0-or-later */
/***********************************************************************
 *
 * Project: Balthazar
 * $Date$
 * $Author$
 *
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
#include "roco_header.h"
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#if KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE

bool cpu_is_imx6s(void)
{
	return of_machine_is_compatible("fsl,imx6dl");
}

bool cpu_is_imx6q(void)
{
	return of_machine_is_compatible("fsl,imx6q");
}

#elif KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE
#include "../arch/arm/mach-imx/hardware.h"

#ifndef __devexit
#define __devexit
#endif

#define cpu_is_imx6s   cpu_is_imx6dl

#else // LINUX_VERSION_CODE
#include "mach/mx6.h"
#define cpu_is_imx6s   cpu_is_mx6dl
#define cpu_is_imx6q   cpu_is_mx6q
#define suspend_late suspend
#define resume_early resume
#endif

// Definitions

// Local prototypes
static long FVD_IOControl(struct file *filep,
			  unsigned int cmd, unsigned long arg);
static int FVD_Open(struct inode *inode, struct file *filp);
static DWORD DoIOControl(PFVD_DEV_INFO pDev,
			 DWORD Ioctl, PUCHAR pBuf, PUCHAR pUserBuf);
static int FVD_mmap(struct file *filep, struct vm_area_struct *vma);
static int fvdk_suspend(struct device *pdev);
static int fvdk_resume(struct device *pdev);

// Local variables
static PFVD_DEV_INFO gpDev;

static char *gpBlob;
static int blobsize;

// Parameters

static int lock_timeout = 3000;
module_param(lock_timeout, int, 0600);
MODULE_PARM_DESC(lock_timeout, "Mutex timeout in ms");

// Code

static int mutex_show(struct device *dev,
		      struct device_attribute *attr,
		      char *buf)
{
	return sprintf(buf,
		       "FPGA file    : %s\n"
		       "Device mutex : %d (%d fails)\n"
		       "Lepton mutex : %d (%d fails)\n"
		       "Exec mutex   : %d (%d fails)\n",
		       gpDev->filename,
		       gpDev->iCtrMuDevice, gpDev->iFailMuDevice,
		       gpDev->iCtrMuLepton, gpDev->iFailMuLepton,
		       gpDev->iCtrMuExecute, gpDev->iFailMuExecute);
}
static DEVICE_ATTR_RO(mutex);

static ssize_t suspend_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	fvdk_suspend(dev);
	return count;
}

static ssize_t resume_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	fvdk_resume(dev);
	return count;
}

static DEVICE_ATTR(suspend, 0200, NULL, suspend_store);
static DEVICE_ATTR(resume, 0200, NULL, resume_store);

static struct attribute *fvd_attrs[] = {
	&dev_attr_resume.attr,
	&dev_attr_suspend.attr,
	&dev_attr_mutex.attr,
	NULL
};

static const struct attribute_group fvd_groups = {
	.attrs = fvd_attrs,
};

static const struct file_operations fvd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = FVD_IOControl,
	.open = FVD_Open,
	.mmap = FVD_mmap,
};

static struct miscdevice fvdk_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fvdk",
	.fops = &fvd_fops
};

static int FVD_mmap(struct file *filep, struct vm_area_struct *vma)
{
	int size;

	size = vma->vm_end - vma->vm_start;

	if (size > blobsize)
		return -EINVAL;
	if (remap_pfn_range(vma, vma->vm_start, __pa(gpBlob) >> PAGE_SHIFT, size, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

// static int fvdk_suspend(struct platform_device *pdev, pm_message_t state)
static int fvdk_suspend(struct device *pdev)
{
	pr_debug("Suspend FVDK driver\n");

	// Power Down
	gpDev->pBSPFvdPowerDownFPA(gpDev);
	gpDev->pBSPFvdPowerDown(gpDev);
	return 0;
}

static int fvdk_resume(struct device *pdev)
{
	int timeout;
	int retval = 0;

	pr_debug("Resume FVDK driver\n");

	// Power Up
	gpDev->pBSPFvdPowerUp(gpDev, TRUE);

	pr_debug("FVDK will load FPGA\n");

	// Load MAIN FPGA
	if (gpDev->spi_flash) {
		gpDev->fpgaLoaded = FALSE;
		return 0;
	}

	retval = LoadFPGA(gpDev, "");
	if (retval != ERROR_SUCCESS) {
		pr_err("fvdk_resume: LoadFPGA failed %d\n", retval);
		return -1;
	}

	// Wait until FPGA loaded
	timeout = 50;
	while (timeout--) {
		msleep(10);
		if (gpDev->pGetPinReady(gpDev) == 0)
			break;
	}

	return 0;
}

static int fvdk_probe(struct platform_device *pdev)
{
	int ret;

	ret = misc_register(&fvdk_miscdev);
	if (ret) {
		pr_err("%s: Failed to register miscdev for FVDK driver\n", __func__);
		goto ERROR_MISC_REGISTER;
	}

#ifdef CONFIG_OF
	gpDev->pLinuxDevice->dev.of_node = of_find_compatible_node(NULL, NULL, "flir,fvd");
	if (of_machine_is_compatible("fsl,imx6dl-ec101")) {
		SetupMX6S_ec101(gpDev);
	} else if (of_machine_is_compatible("fsl,imx6dl-ec501")) {
		SetupMX6S_ec501(gpDev);
	} else if (of_machine_is_compatible("fsl,imx6qp-ec702")) {
		pr_info("ec702 hardware...\n");
		Setup_FLIR_ec702(gpDev);
	} else if (of_machine_is_compatible("fsl,imx6qp-eoco")) {
		pr_info("EOCO Hardware...\n");
		Setup_FLIR_EOCO(gpDev);
	} else
#endif
		if (cpu_is_imx6s()) //AX8
			SetupMX6S(gpDev);
		else if (cpu_is_imx6q()) //T1K
			SetupMX6Q(gpDev);
		else {
			pr_err("%s: Unknown Hardware\n", __func__);
			goto ERROR_UNKNOWN_HARDWARE;
		}

	// DDK not used as DLL to avoid compatibility issues between fvd.dll and OS image
	if (!gpDev->pSetupGpioAccess(gpDev)) {
		pr_err("%s: Error setting up GPIO\n", __func__);
		goto ERROR_GPIO_SETUP;
	}

#if KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE
	ret = sysfs_create_group(&pdev->dev.kobj, &fvd_groups);
	if (ret)
		pr_err("failed to add sys fs entry\n");

#endif

	sema_init(&gpDev->muDevice, 1);
	sema_init(&gpDev->muLepton, 1);
	sema_init(&gpDev->muExecute, 1);
	sema_init(&gpDev->muStandby, 1);

	return 0;

ERROR_GPIO_SETUP:
ERROR_UNKNOWN_HARDWARE:
	misc_deregister(&fvdk_miscdev);
ERROR_MISC_REGISTER:
	return -1;
}

static int fvdk_remove(struct platform_device *pdev)
{
	gpDev->pCleanupGpio(gpDev);
	misc_deregister(&fvdk_miscdev);
	return 0;
}

static const struct dev_pm_ops fvdk_pm_ops = {
	.suspend_late = fvdk_suspend,
	.resume_early = fvdk_resume,
};

static struct platform_driver fvdk_driver = {
	.probe = fvdk_probe,
	.remove = fvdk_remove,
	.driver = {
		   .name = "fvdk",
		   .owner = THIS_MODULE,
		   .pm = &fvdk_pm_ops,
		    },
};

static int __init FVD_Init(void)
{
	int retval = -1;

	// Allocate (and zero-initiate) our control structure.
	gpDev = (PFVD_DEV_INFO) kzalloc(sizeof(FVD_DEV_INFO), GFP_KERNEL);
	if (!gpDev) {
		pr_err("Error allocating memory for pDev, FVD_Init failed\n");
		goto EXIT_OUT;
	}

	gpDev->pLinuxDevice = platform_device_alloc("fvdk", 1);
	if (gpDev->pLinuxDevice == NULL) {
		pr_err("flirdrv-fvdk: Error adding allocating device\n");
		goto EXIT_OUT_PLATFORMALLOC;
	}

	retval = platform_device_add(gpDev->pLinuxDevice);
	if (retval) {
		pr_err("flirdrv-fvdk: Error adding platform device\n");
		goto EXIT_OUT_PLATFORMADD;
	}

	retval = platform_driver_register(&fvdk_driver);
	if (retval < 0) {
		pr_err("flirdrv-fvdk: Error adding platform driver\n");
		goto EXIT_OUT_DRIVERADD;
	}

	gpDev->fpgaLoaded = TRUE;

	return retval;

EXIT_OUT_DRIVERADD:
	platform_device_unregister(gpDev->pLinuxDevice);
EXIT_OUT_PLATFORMADD:
	platform_device_put(gpDev->pLinuxDevice);
EXIT_OUT_PLATFORMALLOC:
	kfree(gpDev);
EXIT_OUT:
	return retval;
}

static void __exit FVD_Deinit(void)
{
	gpDev->pBSPFvdPowerDown(gpDev);
	gpDev->pCleanupGpio(gpDev);
	platform_driver_unregister(&fvdk_driver);
	platform_device_unregister(gpDev->pLinuxDevice);
	kfree(gpDev);
	gpDev = NULL;

	if (gpBlob) {
		kfree(gpBlob);
		gpBlob = NULL;
	}
}

/**
 *  FVD_Open
 *
 * @param inode
 * @param filp
 *
 * @return
 */
static int FVD_Open(struct inode *inode, struct file *filp)
{

	int ret = -1;

	static BOOL init;
	DWORD dwStatus;
	DWORD timeout = 50;

	if (init)
		return 0;

	down(&gpDev->muDevice);

	if (gpDev->spi_flash) {
		// For ROCO the FPGA is configured through an SPI NOR Flash memory,
		// The Norflash is flashed from userspace application
		// Thus, no loading of the FPGA is needed from this driver
		// However We need to read out the Header data configured inte to FLASH
		// the header data from the fpga.bin file is stored 64 kbit from the end of the
		// memory, 2**28 - 2**16 = 256Mbit - 64 kBit
		unsigned char *rxbuf;

		gpDev->pBSPFvdPowerUp(gpDev, FALSE);

		rxbuf = vmalloc(sizeof(unsigned char) * HEADER_LENGTH);
		if (!rxbuf) {
			pr_err("Failed to allocate memory...\n");
			ret = -1;
			goto END;
		}
		ret = read_spi_header(rxbuf);
		if (ret < 0) {
			pr_err("Failed to read data from SPI flash\n");
			goto END;
		}

		memcpy(gpDev->fpga, rxbuf, sizeof(gpDev->fpga));
		ret = 0;

		init = TRUE;   // only if successful open		
END:
		vfree(rxbuf);
		rxbuf = 0;

	} else {
		gpDev->pBSPFvdPowerUp(gpDev, FALSE);

		pr_info("FVD will load FPGA\n");

		// Load MAIN FPGA
		dwStatus = LoadFPGA(gpDev, "");

		if (dwStatus != ERROR_SUCCESS) {
			pr_debug("FVD_Init: LoadFPGA failed %lu\n", dwStatus);
			return -1;
		}

		// Wait until FPGA loaded
		while (timeout--) {
			msleep(10);
			if (gpDev->pGetPinReady(gpDev) == 0)
				break;
		}
		init = TRUE;
		ret = 0;
	}

	up(&gpDev->muDevice);

	return ret;
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
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		dwErr = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
		if (dwErr)
			pr_err("FVD: Copy from user failed: %ld\n", dwErr);
	}

	if (dwErr == ERROR_SUCCESS) {
		dwErr = DoIOControl(gpDev, cmd, tmp, (PUCHAR) arg);
		if (dwErr)
			pr_err("FVD Ioctl %X failed: %ld\n", cmd, dwErr);
	}

	if ((dwErr == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ)) {
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
DWORD DoIOControl(PFVD_DEV_INFO pDev, DWORD Ioctl, PUCHAR pBuf, PUCHAR pUserBuf)
{
	DWORD dwErr = ERROR_INVALID_PARAMETER;

	if (!gpDev->fpgaLoaded) {
		dwErr = down_timeout(&gpDev->muStandby, msecs_to_jiffies(6000));
		if (dwErr)
			return dwErr;
		if (!gpDev->fpgaLoaded) {
			int timeout = 500;

			while (timeout--) {
				msleep(10);
				if (gpDev->pGetPinDone(gpDev))
					break;
			}
			if (timeout < 400)
				pr_info("FVDK conf done timeout %d\n", timeout);
			dwErr = CheckFPGA(gpDev);
			if (dwErr)
				return dwErr;

			// Wait until FPGA ready
			timeout = 50;
			while (timeout--) {
				msleep(10);
				if (gpDev->pGetPinReady(gpDev) == 0)
					break;
			}
			if (timeout < 40)
				pr_info("FVDK ready pin timeout %d\n", timeout);
			gpDev->fpgaLoaded = TRUE;

			up(&gpDev->muStandby);
		}
		dwErr = ERROR_INVALID_PARAMETER;
	}

	switch (Ioctl) {
	case IOCTL_FVDK_GET_VERSION:	// return driver version
		*(ULONG *) pBuf = FVD_VERSION;
		dwErr = ERROR_SUCCESS;
		break;

	case IOCTL_FVDK_POWER_UP:
		pDev->pBSPFvdPowerUp(pDev, FALSE);
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
		memcpy(pBuf, &pDev->fpga[sizeof(GENERIC_FPGA_T)],
		       sizeof(BXAB_FPGA_T));
		dwErr = ERROR_SUCCESS;
		break;

	case IOCTL_FVDK_GET_FPGA_BUF:
		{
			BXAB_FPGA_T *pSpec = (BXAB_FPGA_T *) &pDev->fpga[sizeof(GENERIC_FPGA_T)];

			dwErr = copy_to_user(pUserBuf, &pDev->fpga[sizeof(GENERIC_FPGA_T) + sizeof(BXAB_FPGA_T)], pSpec->noOfBuffers * sizeof(SDRAM_BUF_T));
		}
		break;

	case IOCTL_FVDK_CREATE_BLOB:
		if (gpBlob)
			dwErr = ERROR_SUCCESS;
		else {
			blobsize =
			    (*(ULONG *) pBuf + PAGE_SIZE - 1) & ~(PAGE_SIZE -
								  1);
			gpBlob = kmalloc(blobsize, GFP_KERNEL);
			if (gpBlob == NULL) {
				pr_err("FVDK : Error allocating memory\n");
				blobsize = 0;
				dwErr = -ENOMEM;
			} else {
				memset(gpBlob, 0, blobsize);
				dwErr = ERROR_SUCCESS;
			}
		}
		break;

	case IOCTL_FVDK_LOCK:
		{
			int lock = *(ULONG *) pBuf >> 16;
			int unlock = *(ULONG *) pBuf & 0xFFFF;

			switch (unlock) {
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
			dwErr = ERROR_SUCCESS;
			switch (lock) {
			case LNONE:
				break;
			case LDRV:
				if (lock_timeout)
					dwErr =
					    down_timeout(&gpDev->muDevice,
							 msecs_to_jiffies
							 (lock_timeout));
				else
					down(&gpDev->muDevice);
				pDev->iCtrMuDevice++;
				if (dwErr)
					pDev->iFailMuDevice++;
				break;
			case LEXEC:
				if (lock_timeout)
					dwErr =
					    down_timeout(&gpDev->muExecute,
							 msecs_to_jiffies
							 (lock_timeout));
				else
					down(&gpDev->muExecute);
				pDev->iCtrMuExecute++;
				if (dwErr)
					pDev->iFailMuExecute++;
				break;
			case LLEPT:
				if (lock_timeout)
					dwErr =
					    down_timeout(&gpDev->muLepton,
							 msecs_to_jiffies
							 (lock_timeout));
				else
					down(&gpDev->muLepton);
				pDev->iCtrMuLepton++;
				if (dwErr)
					pDev->iFailMuLepton++;
				break;
			default:
				dwErr = ERROR_INVALID_PARAMETER;
			}
			if (dwErr)
				pr_err("Lock failed %d %d %d\n", unlock, lock,
				       (int)dwErr);
		}
		break;

	default:
		pr_warn("FVDK: Ioctl %lX not supported\n", Ioctl);
		dwErr = ERROR_NOT_SUPPORTED;
		break;
	}

	return dwErr;
}

module_init(FVD_Init);
module_exit(FVD_Deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Video Driver");
MODULE_AUTHOR("Peter Fitger");

//******************************************************************
