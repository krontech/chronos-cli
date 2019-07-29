#include <stdio.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include "IntelHex.h"

IntelHex::IntelHex()
{
	 open = false;
	 addressHi = 0;
}

bool IntelHex::openFile(const char * filename)
{
	infile.open(filename, std::ifstream::in);
	addressHi = 0;
	open = true;
	return true;
}

void IntelHex::close()
{
	if (open)
	{
		infile.close();
		open = false;
	}
}

bool IntelHex::seekToBeginning()
{
	if(open)
	{
		infile.clear();
		infile.seekg(0, std::ios::beg);
		return true;
	}
	return false;

}

//reads a line out of the open intel hex file. If the line doesn't have any valid data, false will be returned
//End of file is indicated by a return value of true and len being set to 0
//
//data - Pointer to array to put read data in
//len - Pointer to uint32 to store number of bytes read
//address - Pointer to uint32 to store the address associated with the returned data
//
//returns - true if valid data was present on the line, false otherwise.
bool IntelHex::readLine(uint8 * data, uint32 * len,  uint32 * address)
{
	std::string line;
	std::stringstream str;
	std::string ss;
	uint8 length;

	if(!open)
		return false;

	while (true)
	{
		std::getline(infile, line);

		if (line.substr(0, 1).compare(":"))
		{
			return false;
		}

		//byte length = Convert.ToByte(line.Substring(1, 2), 16);
		length = strtoul(line.substr(1, 2).c_str(), NULL, 16);

		//addressLo = Convert.ToUInt16(line.Substring(3, 4), 16);
		addressLo = strtoul(line.substr(3, 4).c_str(), NULL, 16);

		//string recordType = line.Substring(7, 2);
		std::string recordType(line.substr(7, 2));

		if (recordType == "00")	//Data record
		{
			for (int i = 0; i < length; i++)
			{
				//data[i] = Convert.ToByte(line.substr(9 + 2 * i, 2), 16);
				data[i] = strtoul(line.substr(9 + 2 * i, 2).c_str(), NULL, 16);
			}
			*len = length;
			*address = (uint32)(addressHi << 16) | addressLo;
			return true;
		}
		else if (recordType == "01") //End of file
		{
			*len = 0;
			*address = 0;
			return true;
		}
		else if (recordType == "04") //Extended Linear Address Record
		{

			//Get address high word and continue reading the next line
			//addressHi = Convert.ToUInt16(line.substr(9, 4), 16);
			addressHi = strtoul(line.substr(9, 4).c_str(), NULL, 16);
		}
		else //Unsupported record type
		{
			return false;
		}
	}

	return false;
}
