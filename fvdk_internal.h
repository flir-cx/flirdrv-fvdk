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

#define FVD_MINOR_VERSION   0
#define FVD_MAJOR_VERSION   1
#define FVD_VERSION ((FVD_MAJOR_VERSION << 16) | FVD_MINOR_VERSION)

// this structure keeps track of the device instance
typedef struct __FVD_DEV_INFO 
{
    // Linux driver variables
	struct platform_device *    pLinuxDevice;
    struct cdev 				fvd_cdev;   	  // Linux character device
    dev_t 						fvd_dev;		  // Major.Minor device number
    struct class               *fvd_class;

    // FPGA header
    char                        fpga[400];                      // FPGA Header data buffer

    // Driver variables
    INT                         giDevices;

    // CPU specific function pointers
    BOOL (*pSetupGpioAccess)(void);
    void (*pCleanupGpio)(struct __FVD_DEV_INFO * pDev);
    BOOL (*pGetPinDone)(void);
    BOOL (*pGetPinStatus)(void);
    BOOL (*pGetPinReady)(void);
    DWORD (*pPutInProgrammingMode)(struct __FVD_DEV_INFO * pDev);
    void (*pBSPFvdPowerUp)(struct __FVD_DEV_INFO * pDev);
    void (*pBSPFvdPowerDown)(struct __FVD_DEV_INFO * pDev);
    void (*pBSPFvdPowerDownFPA)(struct __FVD_DEV_INFO * pDev);
    void (*pBSPFvdPowerUpFPA)(struct __FVD_DEV_INFO * pDev);

    // CPU specific parameters
    int							iSpiBus;
    int							iSpiCountDivisor;
} FVD_DEV_INFO, *PFVD_DEV_INFO;

// Function prototypes to set up hardware specific items
void SetupMX51(PFVD_DEV_INFO pDev);
void SetupMX6S(PFVD_DEV_INFO pDev);

// Function prototypes for common FVD functions
DWORD LoadFPGA(PFVD_DEV_INFO pDev, char* szFileName);
PUCHAR getFPGAData(PFVD_DEV_INFO pDev, ULONG* size, char* out_revision);
void freeFpgaData(void);

#define 	FVD_BSP_PIBB        0
#define 	FVD_BSP_ASBB	    1  // Astra + Nettan

#endif /* __FVD_INTERNAL_H__ */
