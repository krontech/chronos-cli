#include <stdio.h>

#include "fpga.h"
#include "fpga-lux1310.h"

int
main(void)
{
	struct fpga *fpga = fpga_open();
	if (fpga) {
		unsigned long version = fpga->reg[FPGA_VERSION];
		printf("Read FPGA version: 0x%08lx\n", version);
		printf("Read image sensor chipid: 0x%04x\n", fpga_sci_read(fpga, LUX1310_SCI_REV_CHIP));
	}
	printf("Hello World!\n");
	fpga_close(fpga);
	return 0;
}

