/*
 * IntelHex.h
 *
 *  Created on: Mar 26, 2017
 *      Author: david
 */

#ifndef INTELHEX_H_
#define INTELHEX_H_

#include <stdio.h>
#include "types.h"

struct IntelHex {
	FILE *infile;
	unsigned int addressHi;
};

int IntelHexOpen(struct IntelHex *ih, const char * filename);
void IntelHexClose(struct IntelHex *ih);
int IntelHexSeekToBeginning(struct IntelHex *ih);
int IntelHexReadLine(struct IntelHex *ih, uint8 * data, uint32 * len, uint32 * address);

#endif /* INTELHEX_H_ */
