#ifndef ROCO_HEADER_H
#define ROCO_HEADER_H

#define HEADER_LENGTH  65536
#define MTD_DEVICE 0

int read_header(struct mtd_info *mtd, unsigned char *rxbuf);
void prerr_generic_header(GENERIC_FPGA_T * pGen);
void prerr_specific_header(BXAB_FPGA_T * pSpec);
int extract_headers(unsigned char *rxbuf, GENERIC_FPGA_T * pGen,
		    BXAB_FPGA_T * pSpec);
int read_spi_header(unsigned char *rxbuf);
#endif
