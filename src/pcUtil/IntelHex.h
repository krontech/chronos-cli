/*
 * IntelHex.h
 *
 *  Created on: Mar 26, 2017
 *      Author: david
 */

#ifndef INTELHEX_H_
#define INTELHEX_H_

#include <stdio.h>
#include <fstream>
#include "types.h"

class IntelHex
{
public:
	IntelHex();
	bool openFile(const char * filename);
	void close();
	bool seekToBeginning();
	bool readLine(uint8 * data, uint32 * len, uint32 * address);

private:
	std::ifstream infile;
	bool open;
	uint16 addressHi;
	uint16 addressLo;

};


#endif /* INTELHEX_H_ */
