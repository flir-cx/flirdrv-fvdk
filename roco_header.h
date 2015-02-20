#ifndef ROCO_HEADER_H
#define ROCO_HEADER_H
#define	OPCODE_NORM_READ_4B	0x13	/* Read data bytes (low frequency) */
#define FLASH_SIZE 0x10000000 
#define HEADER_LENGTH  65536 
#define HEADER_START_ADDRESS (FLASH_SIZE - HEADER_LENGTH)

int of_dev_node_match(struct device *dev, void *data);
struct spi_device * get_spi_device(const char *ofname);
int check_if_header_exist(struct spi_device *spidev);
unsigned char * read_header(struct spi_device *spidev, unsigned char *rxbuf);
void prerr_generic_header(GENERIC_FPGA_T *pGen);
void prerr_specific_header(BXAB_FPGA_T *pSpec);
int extract_headers(unsigned char *rxbuf, GENERIC_FPGA_T *pGen, BXAB_FPGA_T *pSpec);
int read_spi_header(unsigned char *rxbuf);
#endif
