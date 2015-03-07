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
 *    FPGA load functions
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
#include "fvdk_internal.h"
#include "linux/spi/spi.h"
#include "linux/firmware.h"
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/errno.h>

// Definitions
#define ERROR_NO_INIT_OK        10001
#define ERROR_NO_CONFIG_DONE    10002
#define ERROR_NO_SETUP          10003
#define ERROR_NO_SPI            10004

// Local variables

// Local data
static const struct firmware *pFW;

BOOL GetMainboardVersion(PFVD_DEV_INFO pDev, int *article, int* revision)
{
    struct i2c_msg msgs[2];
    int ret = 1;
    struct i2c_adapter *adap;
    UCHAR addr;
    int iI2c = pDev->iI2c;
    struct
    {
        char article[10];
        char serial[10];
        char revision[4];
        char moduleOffset[2];
        char moduleDevice[2];
        char reserved[2];
        unsigned short checksum;
    } HWrev;    /**< 32 bytes including checksum */

    static int savedArticle;
    static int savedRevision;

    if (!savedArticle)
    {
        adap = i2c_get_adapter(iI2c);
        if(!adap)
            return -ENXIO;

        msgs[0].addr = 0xAE >> 1;
        msgs[1].addr = msgs[0].addr;
        msgs[0].flags = 0;
        msgs[0].len = 1;
        msgs[0].buf = &addr;
        msgs[1].flags = I2C_M_RD | I2C_M_NOSTART;
        msgs[1].len = sizeof(HWrev);
        msgs[1].buf = (UCHAR *)&HWrev;
        addr = 0x40;

        ret = i2c_transfer(adap, msgs, 2);
        i2c_put_adapter(adap);
        if (ret > 0)
        {
            sscanf(HWrev.article, "T%d", &savedArticle);
            sscanf(HWrev.revision, "%d", &savedRevision);
            pr_err("FVD: Mainboard article %d revision %d\n", savedArticle, savedRevision);
        }
        else
        {
            pr_err("FVD: Failed reading article (%d)\n", ret);
        }
    }
    *article = savedArticle;
    *revision = savedRevision;

    return (ret > 0);
}

#define FW_DIR "FLIR/"

// Code
PUCHAR getFPGAData(PFVD_DEV_INFO pDev,
                   ULONG* size,
                   char* pHeader)
{
    GENERIC_FPGA_T* pGen;
    BXAB_FPGA_T* pSpec;
    int err;
    int article=0, revision=0;

    GetMainboardVersion (pDev,&article, &revision);
    switch (article)
    {
    case 198606:
        if (revision >= 4)
            pDev->filename = FW_DIR "fpga_neco_c.bin";
        else
            pDev->filename = FW_DIR "fpga_neco_b.bin";
        break;

    default:
        pDev->filename = FW_DIR "fpga.bin";
        break;
    }

    /* Request firmware from user space */
    err = request_firmware(&pFW, pDev->filename, &pDev->pLinuxDevice->dev);
    if (err)
    {
        pr_err("Failed to get file %s\n", pDev->filename);
        return(NULL);
    }

    pr_err("Got %d bytes of firmware from %s\n", pFW->size, pDev->filename);

    /* Read generic header */
    if (pFW->size < sizeof(GENERIC_FPGA_T))
    {
        return(NULL);
    }
    pGen = (GENERIC_FPGA_T *)pFW->data;
    if (pGen->headerrev > GENERIC_REV)
    {
        return(NULL);
    }
    if (pGen->spec_size > 1024)
    {
        return(NULL);
    }

    /* Read specific part */
    if (pFW->size < (sizeof(GENERIC_FPGA_T) + pGen->spec_size))
    {
        return(NULL);
    }
    pSpec = (BXAB_FPGA_T *)&pFW->data[sizeof(GENERIC_FPGA_T)];

    /* Set FW size */
    *size = pFW->size - sizeof(GENERIC_FPGA_T) - pGen->spec_size;

    memcpy (pHeader, pFW->data, sizeof(GENERIC_FPGA_T) + pGen->spec_size);
    return((PUCHAR)&pFW->data[sizeof(GENERIC_FPGA_T) + pGen->spec_size]);
}

void freeFpgaData(void)
{
    if (pFW)
        release_firmware(pFW);
    pFW = NULL;
}

DWORD CheckFPGA(PFVD_DEV_INFO pDev)
{
    DWORD res = ERROR_SUCCESS;

    if(0 == pDev->pGetPinDone())
    {
        if(0 == pDev->pGetPinStatus())
            res = ERROR_NO_INIT_OK;
        else
            res = ERROR_NO_CONFIG_DONE;
        pr_err("CheckFPGA: FPGA load failed (%ld)\n", res);
    }

    return res;
}

 struct spi_board_info chip = {
    .modalias = "fvdspi",
    .max_speed_hz = 50000000,
    .mode = SPI_MODE_0,
};

#define tms(x) (x.tv_sec*1000 + x.tv_usec/1000)

DWORD LoadFPGA(PFVD_DEV_INFO pDev, char* szFileName)
{
    DWORD res = ERROR_SUCCESS;
    unsigned long size;
    unsigned char *fpgaBin;
    int ret;
	struct spi_master *pspim;
	struct spi_device *pspid;
	struct timeval t[10];

	do_gettimeofday(&t[0]);

    // read file
    fpgaBin = getFPGAData(pDev, &size, pDev->fpga);
    if (NULL == fpgaBin)
    {
        pr_err("LoadFPGA: Error reading %s\n", szFileName);
        return ERROR_IO_DEVICE;
    }

    if (pDev->pGetPinDone())
    {
        pr_err ("LoadFPGA: Fpga has already been programmed in uboot");
        goto done;
        //platforms with pcie loads fpga in u-boot
    }

	do_gettimeofday(&t[1]);

    // swap bit and byte order
    if (((GENERIC_FPGA_T *)(pDev->fpga))->LSBfirst)
    {
        ULONG *ptr = (ULONG *)fpgaBin;
        int len = (size+3)/4;
        static const char reverseNibble[16] = 
        { 0x00, 0x08, 0x04, 0x0C,   // 0, 1, 2, 3
          0x02, 0x0A, 0x06, 0x0E,   // 4, 5, 6, 7
          0x01, 0x09, 0x05, 0x0D,   // 8, 9, A, B
          0x03, 0x0B, 0x07, 0x0F }; // C, D, E, F

        while (len--)
        {
            ULONG tmp = *ptr;
            *ptr = reverseNibble[tmp >> 28] |
                  (reverseNibble[(tmp >> 24) & 0x0F] << 4) |
                  (reverseNibble[(tmp >> 20) & 0x0F] << 8) |
                  (reverseNibble[(tmp >> 16) & 0x0F] << 12) |
                  (reverseNibble[(tmp >> 12) & 0x0F] << 16) |
                  (reverseNibble[(tmp >> 8) & 0x0F] << 20) |
                  (reverseNibble[(tmp >> 4) & 0x0F] << 24) |
                  (reverseNibble[tmp & 0x0F] << 28);
            ptr++;
        }
    }
    else
    {
        ULONG *ptr = (ULONG *)fpgaBin;
        int len = (size+3)/4;

        while (len--)
        {
            ULONG tmp = *ptr;
            *ptr = (tmp >> 24) |
                ((tmp >> 8) & 0xFF00) |
                ((tmp << 8) & 0xFF0000) |
                (tmp << 24);
            ptr++;
        }
    }

    pr_err("LoadFPGA: Activating programming mode\n");

	do_gettimeofday(&t[2]);

    // Put FPGA in programming mode
    if (pDev->pPutInProgrammingMode(pDev) == 0)
    {
    	msleep(5);
        if (pDev->pPutInProgrammingMode(pDev) == 0)
        {
            pr_err("LoadFPGA: Failed to set FPGA in programming mode\n");
            freeFpgaData();
            return ERROR_NO_SETUP;
        }
    }

	do_gettimeofday(&t[3]);

    pr_err("LoadFPGA: Sending FPGA code over SPI%d\n", pDev->iSpiBus);

    // Send FPGA code through SPI
    pspim = spi_busnum_to_master(pDev->iSpiBus);
    if (pspim == 0)
    {
        pr_err("LoadFPGA: Failed to get SPI master\n");
    	return ERROR_NO_SPI;
    }
	pspid = spi_new_device(pspim, &chip);
    if (pspid == 0)
    {
        pr_err("LoadFPGA: Failed to set SPI device\n");
    	return ERROR_NO_SPI;
    }
	pspid->bits_per_word = 32;
	ret = spi_setup(pspid);
    ret = spi_write(pspid, fpgaBin,
    		((size/pDev->iSpiCountDivisor) + pDev->iSpiCountDivisor - 1) & ~3);

    device_unregister(&pspid->dev);
	put_device(&pspim->dev);

	do_gettimeofday(&t[4]);

    //programming OK?
    res = CheckFPGA(pDev);

	do_gettimeofday(&t[5]);

    // Printing mesage here breaks startup timing for SB 0601 detectors
    pr_err("FPGA loaded in %ld ms (read %ld rotate %ld prep %ld SPI %ld check %ld)\r\n",
    		tms(t[5]) - tms(t[0]),
    		tms(t[1]) - tms(t[0]),
    		tms(t[2]) - tms(t[1]),
    		tms(t[3]) - tms(t[2]),
    		tms(t[4]) - tms(t[3]),
    		tms(t[5]) - tms(t[4]));
done:
    freeFpgaData();
    return res;
}
