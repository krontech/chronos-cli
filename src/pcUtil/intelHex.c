#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "IntelHex.h"

int IntelHexOpen(struct IntelHex *ih, const char * filename)
{
	ih->infile = fopen(filename, "r");
	ih->addressHi = 0;
	return (ih->infile != NULL);
}

void IntelHexClose(struct IntelHex *ih)
{
	if (ih->infile)
	{
		fclose(ih->infile);
		ih->infile = NULL;
	}
}

int IntelHexSeekToBeginning(struct IntelHex *ih)
{
	if(ih->infile)
	{
		fseek(ih->infile, 0, SEEK_SET);
		return TRUE;
	}
	return FALSE;

}

/* Quick and dirty helper - parse a single byte from hex */
static unsigned int strtobyte(const char *str, unsigned int offset)
{
	if (memchr(str, '\0', offset) != NULL) {
		/* The offset has overflowed the input string */
		return 0;
	} else {
		char hex[3] = {str[offset + 0], str[offset + 1], '\0'};
		return strtoul(hex, NULL, 16);
	}
}

/* Quick and dirty helper - parse a 16-bit big endian word */
static unsigned int strtou16(const char *str, unsigned int offset)
{
	if (memchr(str, '\0', offset) != NULL) {
		/* The offset has overflowed the input string */
		return 0;
	} else {
		char hex[5] = {str[offset + 0], str[offset + 1], str[offset + 2], str[offset + 3], '\0'};
		return strtoul(hex, NULL, 16);
	}
}

//reads a line out of the open intel hex file. If the line doesn't have any valid data, false will be returned
//End of file is indicated by a return value of true and len being set to 0
//
//data - Pointer to array to put read data in
//len - Pointer to uint32 to store number of bytes read
//address - Pointer to uint32 to store the address associated with the returned data
//
//returns - true if valid data was present on the line, false otherwise.
int IntelHexReadLine(struct IntelHex *ih, uint8 * data, uint32 * len,  uint32 * address)
{
	char linebuf[256];
	char *recordType;
	unsigned int length;
	unsigned int addressLo;

	if(!ih->infile)
		return FALSE;

	while (TRUE)
	{
		if (!fgets(linebuf, sizeof(linebuf), ih->infile)) {
			return FALSE;
		}
		if ((linebuf[0] != ':') || (strlen(linebuf) < 9))
		{
			return FALSE;
		}

		//byte length = Convert.ToByte(line.Substring(1, 2), 16);
		length = strtobyte(linebuf, 1);

		//addressLo = Convert.ToUInt16(line.Substring(3, 4), 16);
		addressLo = strtou16(linebuf, 3);

		//string recordType = line.Substring(7, 2);
		recordType = linebuf + 7;
		if (memcmp(recordType, "00", 2) == 0) {	//Data record
			int i;
			for (i = 0; i < length; i++)
			{
				//data[i] = Convert.ToByte(line.substr(9 + 2 * i, 2), 16);
				data[i] = strtobyte(linebuf, 9 + 2 * i);
			}
			*len = length;
			*address = (uint32)(ih->addressHi << 16) | addressLo;
			return TRUE;
		}
		else if (memcmp(recordType, "01", 2) == 0) //End of file
		{
			*len = 0;
			*address = 0;
			return TRUE;
		}
		else if (memcmp(recordType, "04", 2) == 0) //Extended Linear Address Record
		{
			//Get address high word and continue reading the next line
			//addressHi = Convert.ToUInt16(line.substr(9, 4), 16);
			ih->addressHi = strtou16(linebuf, 9);
		}
		else //Unsupported record type
		{
			return FALSE;
		}
	}

	return FALSE;
}
