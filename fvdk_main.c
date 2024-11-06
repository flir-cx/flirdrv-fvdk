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

// Definitions

// Local prototypes
static long FVD_IOControl(struct file *file, unsigned int cmd, unsigned long arg);
static int FVD_Open(struct inode *inode, struct file *file);
static int FVD_mmap(struct file *file, struct vm_area_struct *vma);
static int fvdk_suspend(struct device *dev);
static int fvdk_resume(struct device *dev);
static ssize_t resume_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count);
static ssize_t suspend_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count);

// Parameters

static int lock_timeout = 3000;
module_param(lock_timeout, int, 0600);
MODULE_PARM_DESC(lock_timeout, "Mutex timeout in ms");

// Code

static const struct file_operations fvd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = FVD_IOControl,
	.open = FVD_Open,
	.mmap = FVD_mmap,
};

static DEVICE_ATTR_WO(suspend);
static DEVICE_ATTR_WO(resume);

static struct attribute *fvdk_sysfs_attrs[] = {
	&dev_attr_resume.attr,
	&dev_attr_suspend.attr,
	NULL
};

static const struct attribute_group fvdk_sysfs_group = {
	.name = "control",
	.attrs = fvdk_sysfs_attrs,
};

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

static int FVD_mmap(struct file *file, struct vm_area_struct *vma)
{
	int size;
	struct fvdkdata *data = container_of(file->private_data, struct fvdkdata, miscdev);

	size = vma->vm_end - vma->vm_start;

	if (size > (data->pDev.blobsize))
		return -EINVAL;
	if (remap_pfn_range(vma, vma->vm_start, __pa(data->pDev.blob) >> PAGE_SHIFT,
			    size, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

// static int fvdk_suspend(struct platform_device *pdev, pm_message_t state)
static int fvdk_suspend(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	dev_dbg(dev, "Suspend FVDK driver\n");

	// Power Down
	data->ops.pBSPFvdPowerDownFPA(dev);
	data->ops.pBSPFvdPowerDown(dev);
	return 0;
}

static int fvdk_resume(struct device *dev)
{
	int timeout;
	int retval = 0;
	struct fvdkdata *data = dev_get_drvdata(dev);

	dev_dbg(dev, "Resume FVDK driver\n");

	// Power Up
	data->ops.pBSPFvdPowerUp(dev, TRUE);

	dev_dbg(dev, "FVDK will load FPGA\n");

	// Load MAIN FPGA
	if (data->pDev.spi_flash) {
		data->pDev.fpgaLoaded = FALSE;
		return 0;
	}

	retval = LoadFPGA(dev, "");
	if (retval != ERROR_SUCCESS) {
		dev_err(dev, "LoadFPGA failed %d\n", retval);
		return -1;
	}

	// Wait until FPGA loaded
	timeout = 50;
	while (timeout--) {
		msleep(10);
		if (data->ops.pGetPinReady(dev) == 0)
			break;
	}

	return 0;
}

static int fvdk_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct fvdkdata *data;

	data = devm_kzalloc(dev, sizeof(struct fvdkdata), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pDev.fpgaLoaded = TRUE;
	data->dev = dev;

	dev_set_drvdata(dev, data);
	platform_set_drvdata(pdev, data);

	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = "fvdk";
	data->miscdev.fops = &fvd_fops;
	data->miscdev.parent = dev;

	ret = misc_register(&data->miscdev);
	if (ret) {
		dev_err(dev, "%s: Failed to register miscdev for FVDK driver\n", __func__);
		return -EIO;
	}

	if (of_machine_is_compatible("fsl,imx6dl-ec101")) {
		SetupMX6S_ec101(dev);
	} else if (of_machine_is_compatible("fsl,imx6dl-ec501")) {
		SetupMX6S_ec501(dev);
	} else if (of_machine_is_compatible("fsl,imx6qp-ec702")) {
		Setup_FLIR_ec702(dev);
	} else if (of_machine_is_compatible("fsl,imx6qp-eoco")) {
		Setup_FLIR_EOCO(dev);
	} else {
		dev_err(dev, "%s: Unknown Hardware\n", __func__);
		goto ERROR_UNKNOWN_HARDWARE;
	}

	// DDK not used as DLL to avoid compatibility issues between fvd.dll and OS image
	if (!data->ops.pSetupGpioAccess(dev)) {
		dev_err(dev, "%s: Error setting up GPIO\n", __func__);
		goto ERROR_GPIO_SETUP;
	}

	ret = sysfs_create_group(&dev->kobj, &fvdk_sysfs_group);
	if (ret)
		dev_err(dev, "%s: Failed to add sysfs entries\n", __func__);

	if (data->ops.pGetPinReady(dev) != 0) {
		int r;

		dev_dbg(dev, "Resuming FPGA");
		r = fvdk_resume(dev);
		dev_err(dev, "FVDK Resume %i", r);
	}

	sema_init(&(data->muDevice), 1);
	sema_init(&(data->muLepton), 1);
	sema_init(&(data->muExecute), 1);
	sema_init(&(data->muStandby), 1);

	return 0;

ERROR_GPIO_SETUP:
ERROR_UNKNOWN_HARDWARE:
	misc_deregister(&data->miscdev);
	return -1;
}

static int fvdk_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fvdkdata *data = dev_get_drvdata(dev);

	data->ops.pBSPFvdPowerDownFPA(dev);
	data->ops.pBSPFvdPowerDown(dev);

	kfree(data->pDev.blob);
	data->pDev.blob = NULL;

	sysfs_remove_group(&dev->kobj, &fvdk_sysfs_group);
	data->ops.pCleanupGpio(dev);
	misc_deregister(&data->miscdev);
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


/**
 *  FVD_Open
 *
 * @param inode
 * @param file
 *
 * @return
 */
static int FVD_Open(struct inode *inode, struct file *file)
{

	int ret = -1;
	struct fvdkdata *data = container_of(file->private_data, struct fvdkdata, miscdev);
	struct device *dev = data->dev;
	static BOOL init;
	DWORD dwStatus;
	DWORD timeout = 50;

	if (init)
		return 0;

	down(&(data->muDevice));

	if (data->pDev.spi_flash) {
		// For ROCO the FPGA is configured through an SPI NOR Flash memory,
		// The Norflash is flashed from userspace application
		// Thus, no loading of the FPGA is needed from this driver
		// However We need to read out the Header data configured inte to FLASH
		// the header data from the fpga.bin file is stored 64 kbit from the end of the
		// memory, 2**28 - 2**16 = 256Mbit - 64 kBit
		unsigned char *rxbuf;

		data->ops.pBSPFvdPowerUp(dev, FALSE);

		rxbuf = vmalloc(sizeof(unsigned char) * HEADER_LENGTH);
		if (!rxbuf) {
			ret = -1;
			goto SPI_FAIL;
		}
		ret = read_spi_header(rxbuf);
		if (ret < 0) {
			dev_err(dev, "Failed to read data from SPI flash\n");
			goto SPI_FAIL;
		}

		memcpy(data->pDev.fpga, rxbuf, sizeof(data->pDev.fpga));
		ret = 0;

		init = TRUE;   // only if successful open
SPI_FAIL:
		vfree(rxbuf);
		rxbuf = 0;

	} else {
		data->ops.pBSPFvdPowerUp(dev, FALSE);

		dev_info(dev, "FVD will load FPGA\n");

		// Load MAIN FPGA
		dwStatus = LoadFPGA(dev, "");

		if (dwStatus != ERROR_SUCCESS) {
			dev_err(dev, "FVD_Init: LoadFPGA failed %lu\n", dwStatus);
			ret = -1;
			goto END;
		}

		// Wait until FPGA loaded
		while (timeout--) {
			msleep(10);
			if (data->ops.pGetPinReady(dev) == 0)
				break;
		}
		init = TRUE;
		ret = 0;
	}
END:
	up(&(data->muDevice));

	return ret;
}

static long FVD_IOControl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err = ERROR_SUCCESS;
	char *tmp;
	struct fvdkdata *data = container_of(file->private_data, struct fvdkdata, miscdev);
	struct device *dev = data->dev;

	tmp = kzalloc(_IOC_SIZE(cmd), GFP_KERNEL);
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		err = copy_from_user(tmp, (void *)arg, _IOC_SIZE(cmd));
		if (err)
			dev_err(dev, "FVD: Copy from user failed: %ld\n", err);
	}

	if (err == ERROR_SUCCESS) {
		switch (cmd) {
		case IOCTL_FVDK_GET_VERSION:	// return driver version
			*(ULONG *) tmp = FVD_VERSION;
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_POWER_UP:
			data->ops.pBSPFvdPowerUp(dev, FALSE);
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_POWER_DOWN:
			data->ops.pBSPFvdPowerDown(dev);
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_POWER_UP_FPA:
			data->ops.pBSPFvdPowerUpFPA(dev);
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_POWER_DOWN_FPA:
			data->ops.pBSPFvdPowerDownFPA(dev);
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_GET_FPGA_GENERIC:
			memcpy(tmp, &(data->pDev.fpga[0]), sizeof(GENERIC_FPGA_T));
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_GET_FPGA_DATA:
			memcpy(tmp, &(data->pDev.fpga[sizeof(GENERIC_FPGA_T)]),
			       sizeof(BXAB_FPGA_T));
			err = ERROR_SUCCESS;
			break;

		case IOCTL_FVDK_GET_FPGA_BUF:
		{
			BXAB_FPGA_T *pSpec =
				(BXAB_FPGA_T *) &(data->pDev.fpga[sizeof(GENERIC_FPGA_T)]);

			err = copy_to_user((void *)arg,
					   &(data->pDev.fpga[sizeof(GENERIC_FPGA_T) +
							     sizeof(BXAB_FPGA_T)]),
					   pSpec->noOfBuffers * sizeof(SDRAM_BUF_T));
		}
		break;

		case IOCTL_FVDK_CREATE_BLOB:
			if (data->pDev.blob)
				err = ERROR_SUCCESS;
			else {
				data->pDev.blobsize =
					(*(ULONG *) tmp + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
				data->pDev.blob = kmalloc(data->pDev.blobsize, GFP_KERNEL);
				if (data->pDev.blob == NULL) {
					dev_err(dev, "FVDK : Error allocating memory\n");
					data->pDev.blobsize = 0;
					err = -ENOMEM;
				} else {
					memset(data->pDev.blob, 0, data->pDev.blobsize);
					err = ERROR_SUCCESS;
				}
			}
			break;

		case IOCTL_FVDK_LOCK:
		{
			int lock = *(ULONG *) tmp >> 16;
			int unlock = *(ULONG *) tmp & 0xFFFF;

			switch (unlock) {
			case LDRV:
				up(&(data->muDevice));
				break;
			case LEXEC:
				up(&(data->muExecute));
				break;
			case LLEPT:
				up(&(data->muLepton));
				break;
			}
			err = ERROR_SUCCESS;
			switch (lock) {
			case LNONE:
				break;
			case LDRV:
				if (lock_timeout)
					err = down_timeout(&(data->muDevice),
							   msecs_to_jiffies(lock_timeout));
				else
					down(&(data->muDevice));
				break;
			case LEXEC:
				if (lock_timeout)
					err = down_timeout(&(data->muExecute),
							   msecs_to_jiffies(lock_timeout));
				else
					down(&(data->muExecute));
				break;
			case LLEPT:
				if (lock_timeout)
					err = down_timeout(&(data->muLepton),
							   msecs_to_jiffies(lock_timeout));
				else
					down(&(data->muLepton));
				break;
			default:
				err = ERROR_INVALID_PARAMETER;
			}
			if (err)
				dev_err(dev, "Lock failed %d %d %d\n", unlock, lock, (int)err);
		}
		break;

		default:
			dev_warn(dev, "FVDK: Ioctl %u not supported\n", cmd);
			err = ERROR_NOT_SUPPORTED;
			break;
		}
	}

	if (err)
		dev_err(dev, "FVD Ioctl %X failed: %ld\n", cmd, err);

	if ((err == ERROR_SUCCESS) && (_IOC_DIR(cmd) & _IOC_READ)) {
		err = copy_to_user((void *)arg, tmp, _IOC_SIZE(cmd));
		if (err)
			dev_err(dev, "FVD: Copy to user failed: %ld\n", err);
	}
	kfree(tmp);

	return err;
}

module_platform_driver(fvdk_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FLIR Video Driver");
MODULE_AUTHOR("Peter Fitger");
