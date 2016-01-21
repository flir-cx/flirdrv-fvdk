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
 *    Internal structs and definitions
 *
 * Last check-in changelist:
 * $Change$
 * 
 *
 * Copyright: FLIR Systems AB.  All rights reserved.
 *
 ***********************************************************************/

#ifndef __FVD_INTERNAL_H__
#define __FVD_INTERNAL_H__

#include <linux/proc_fs.h>
#include <linux/regulator/consumer.h>

#define FVD_MINOR_VERSION   0
#define FVD_MAJOR_VERSION   1
#define FVD_VERSION ((FVD_MAJOR_VERSION << 16) | FVD_MINOR_VERSION)

// this structure keeps track of the device instance
typedef struct __FVD_DEV_INFO 
{
	// Linux driver variables
	struct platform_device 	*pLinuxDevice;
	struct cdev		fvd_cdev;	// Linux character device
	dev_t 			fvd_dev;	// Major.Minor device number
	struct class		*fvd_class;
	struct proc_dir_entry	*proc;		// proc fs entry
	BOOL			fpgaLoaded;

	// FPGA header
	char			fpga[400];	// FPGA Header data buffer
	char			*filename;

	// CPU specific function pointers
	BOOL (*pSetupGpioAccess)(struct __FVD_DEV_INFO * pDev);
	void (*pCleanupGpio)(struct __FVD_DEV_INFO * pDev);
	BOOL (*pGetPinDone)(struct __FVD_DEV_INFO * pDev);
	BOOL (*pGetPinStatus)(struct __FVD_DEV_INFO * pDev);
	BOOL (*pGetPinReady)(struct __FVD_DEV_INFO * pDev);
	DWORD (*pPutInProgrammingMode)(struct __FVD_DEV_INFO * pDev);
	void (*pBSPFvdPowerUp)(struct __FVD_DEV_INFO * pDev, BOOL restart);
	void (*pBSPFvdPowerDown)(struct __FVD_DEV_INFO * pDev);
	void (*pBSPFvdPowerDownFPA)(struct __FVD_DEV_INFO * pDev);
	void (*pBSPFvdPowerUpFPA)(struct __FVD_DEV_INFO * pDev);

    //GPIOs
    int program_gpio;
    int init_gpio;
    int conf_done_gpio;
    int ready_gpio;

#ifdef CONFIG_OF
    struct device_node * np;
#endif

    //Regulators
	struct regulator *reg_detector;
	struct regulator *reg_mems;
	struct regulator *reg_4v0_fpa;
	struct regulator *reg_3v15_fpa;

    //Configs
    bool spi_flash;

	// Locks
	struct semaphore            muDevice;
	struct semaphore            muLepton;
	struct semaphore            muExecute;
	struct semaphore            muStandby;

	// CPU specific parameters
	int							iSpiBus;
	int							iSpiCountDivisor;
	int                         iI2c;   //Main i2c bus (eeprom)

	// Statistics
	int iCtrMuDevice;
	int iCtrMuLepton;
	int iCtrMuExecute;
	int iFailMuDevice;
	int iFailMuLepton;
	int iFailMuExecute;
} FVD_DEV_INFO, *PFVD_DEV_INFO;

// Function prototypes to set up hardware specific items
void SetupMX51(PFVD_DEV_INFO pDev);
void SetupMX6S(PFVD_DEV_INFO pDev);
void SetupMX6Q(PFVD_DEV_INFO pDev);
void SetupMX6S_ec101(PFVD_DEV_INFO pDev);

// Function prototypes for common FVD functions
DWORD CheckFPGA(PFVD_DEV_INFO pDev);
DWORD LoadFPGA(PFVD_DEV_INFO pDev, char* szFileName);
PUCHAR getFPGAData(PFVD_DEV_INFO pDev, ULONG* size, char* out_revision);
void freeFpgaData(void);
BOOL GetMainboardVersion(PFVD_DEV_INFO pDev, int *article, int* revision);

#define 	FVD_BSP_PIBB        0
#define 	FVD_BSP_ASBB	    1  // Astra + Nettan

#define ROCO_ARTNO			198752 //T198752 ROCO  mainboard article no (Rocky)

enum locks { LNONE, LDRV, LEXEC, LLEPT };

#endif /* __FVD_INTERNAL_H__ */
