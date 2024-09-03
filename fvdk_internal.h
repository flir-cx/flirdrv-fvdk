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
#include <linux/miscdevice.h>

#define FVD_MINOR_VERSION   0
#define FVD_MAJOR_VERSION   1
#define FVD_VERSION ((FVD_MAJOR_VERSION << 16) | FVD_MINOR_VERSION)

struct fvdk_ops {
	// CPU specific function pointers
	BOOL (*pSetupGpioAccess)(struct device *dev);
	void (*pCleanupGpio)(struct device *dev);
	BOOL (*pGetPinDone)(struct device *dev);
	BOOL (*pGetPinStatus)(struct device *dev);
	BOOL (*pGetPinReady)(struct device *dev);
	DWORD (*pPutInProgrammingMode)(struct device *dev);
	void (*pBSPFvdPowerUp)(struct device *dev, BOOL restart);
	void (*pBSPFvdPowerDown)(struct device *dev);
	void (*pBSPFvdPowerDownFPA)(struct device *dev);
	void (*pBSPFvdPowerUpFPA)(struct device *dev);
};

struct fpga_pins {
	// GPIOS altera fpga
	int pin_fpga_ce_n;
	int pin_fpga_config_n;
	int pin_fpga_conf_done;
	int pin_fpga_status_n;

  // GPIOs Xilinx fpga
	int program_gpio;
	int init_gpio;
	int conf_done_gpio;
	int ready_gpio;
};

// this structure keeps track of the device instance
typedef struct __FVD_DEV_INFO {
	// Linux driver variables
	BOOL fpgaLoaded;

	// FPGA header
	char fpga[400];		// FPGA Header data buffer

	int spi_sclk_gpio;
	int spi_mosi_gpio;
	int spi_miso_gpio;
	int spi_cs_gpio;

	char *blob;
	int blobsize;

	//Configs
	bool spi_flash;

	// CPU specific parameters
	int iSpiBus;
	int iSpiCountDivisor;
	int iI2c;		//Main i2c bus (eeprom)

} FVD_DEV_INFO, *PFVD_DEV_INFO;

struct fvdkdata {
	struct fvdk_ops ops;
	struct miscdevice miscdev;
	struct device *dev;
	FVD_DEV_INFO pDev;
	//Regulators
	struct regulator *reg_4v0_fpa;
	struct regulator *reg_3v15_fpa;
	struct regulator *reg_fpa_i2c;
	struct regulator *reg_1v0_fpga;
	struct regulator *reg_1v1_fpga;
	struct regulator *reg_1v2_fpga;
	struct regulator *reg_1v8_fpga;
	struct regulator *reg_2v5_fpga;
	struct regulator *reg_3v15_fpga;

	// Pinmux
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_idle;

	// Locks
	struct semaphore muDevice;
	struct semaphore muLepton;
	struct semaphore muExecute;
	struct semaphore muStandby;

	struct fpga_pins fpga_pins;
};

// Function prototypes to set up hardware specific items
void SetupMX6S_ec101(struct device *dev);
void SetupMX6S_ec501(struct device *dev);
void Setup_FLIR_EOCO(struct device *dev);
void Setup_FLIR_ec702(struct device *dev);

// Function prototypes for common FVD functions
DWORD CheckFPGA(struct device *dev);
DWORD LoadFPGA(struct device *dev, char *szFileName);
PUCHAR getFPGAData(struct device *dev, ULONG *size, char *out_revision);
void freeFpgaData(void);
BOOL GetMainboardVersion(struct device *dev, int *article, int *revision);

#define	FVD_BSP_PIBB  0
#define	FVD_BSP_ASBB  1	// Astra + Nettan

#define ROCO_ARTNO 198752	//T198752 ROCO  mainboard article no (Rocky)
#define EC101_ARTNO 199051	//T199051 ec101  mainboard article no (Evander)

enum locks { LNONE, LDRV, LEXEC, LLEPT };

#endif /* __FVD_INTERNAL_H__ */

