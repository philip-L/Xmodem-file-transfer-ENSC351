//============================================================================
//
//% Student Name 1: Adnan Syed
//% Student 1 #: 301247513
//% Student 1 userid (email): ssyed (ssyed@sfu.ca)
//
//% Student Name 2: Philip Leblanc
//% Student 2 #: 301248211
//% Student 2 userid (email): paleblan (paleblan@sfu.ca)
//
//% Below, edit to list any people who helped you with the code in this file,
//%      or put 'None' if nobody helped (the two of) you.
//
// Helpers: _everybody helped us/me with the assignment (list names or put 'None')__
// Eldon Lin, Dayton Pukanich
//
// Also, list any resources beyond the course textbooks and the course pages on Piazza
// that you used in making your submission.
//
// Resources:  Piazza Post number 31
//
//%% Instructions:
//% * Put your name(s), student number(s), userid(s) in the above section.
//% * Also enter the above information in other files to submit.
//% * Edit the "Helpers" line and, if necessary, the "Resources" line.
//% * Your group name should be "P1_<userid1>_<userid2>" (eg. P1_stu1_stu2)
//% * Form groups as described at:  https://courses.cs.sfu.ca/docs/students
//% * Submit files to courses.cs.sfu.ca
//
// File Name   : SenderX.cc
// Version     : September 3rd, 2017
// Description : Starting point for ENSC 351 Project
// Original portions Copyright (c) 2017 Craig Scratchley  (wcs AT sfu DOT ca)
//============================================================================

#include <iostream>
#include <stdint.h> // for uint8_t
#include <string.h> // for memset()
#include <errno.h>
#include <fcntl.h>	// for O_RDWR

#include "myIO.h"
#include "SenderX.h"

using namespace std;

SenderX::SenderX(const char *fname, int d)
:PeerX(d, fname), bytesRd(-1), blkNum(255)
{
}

//-----------------------------------------------------------------------------

/* tries to generate a block.  Updates the
variable bytesRd with the number of bytes that were read
from the input file in order to create the block. Sets
bytesRd to 0 and does not actually generate a block if the end
of the input file had been reached when the previously generated block was
prepared or if the input file is empty (i.e. has 0 length).
*/
void SenderX::genBlk(blkT blkBuf)
{
	// ********* The next line needs to be changed ***********
	if (-1 == (bytesRd = myRead(transferringFileD, &blkBuf[3], CHUNK_SZ ))){
		ErrorPrinter("myRead(transferringFileD, &blkBuf[0], CHUNK_SZ )", __FILE__, __LINE__, errno);
	}
	// ********* and additional code must be written ***********

	//Enter padding for the rest of the block
	for(int i=bytesRd; i < CHUNK_SZ; i++ ){
		blkBuf[i+3] = CTRL_Z;
	}

	int checkSum = 0;

	blkBuf[0] = SOH;      // The Header
	blkBuf[1] = blkNum%256; //The block number
	blkBuf[2] = 255 - blkNum; //should make it one complement

	if(Crcflg){
		uint16_t temp;
		crc16ns(&temp, &blkBuf[3]);

		blkBuf[131] = temp>>8;
		blkBuf[132] = temp;

	}

	else{
		for(int i = 0; i < 128; i++){
			checkSum = checkSum + blkBuf[i+3];
		}

		blkBuf[131] = checkSum%256;

	}

}

void SenderX::sendFile()
{
	transferringFileD = myOpen(fileName, O_RDWR, 0);
	if(transferringFileD == -1) {
		cout /* cerr */ << "Error opening input file named: " << fileName << endl;
		result = "OpenError";
	}
	else {
		cout << "Sender will send " << fileName << endl;

		blkNum = 1; // but first block sent will be block #1, not #0

		// do the protocol, and simulate a receiver that positively acknowledges every
		//	block that it receives.

		// assume 'C' or NAK received from receiver to enable sending with CRC or checksum, respectively
		genBlk(blkBuf); // prepare 1st block
		while (bytesRd)
		{
			blkNum ++; // 1st block about to be sent or previous block was ACK'd

			// ********* fill in some code here to send a block ***********
			if(Crcflg){
				myWrite( mediumD, &blkBuf, BLK_SZ_CRC);
			}
			else{
				myWrite( mediumD, &blkBuf, BLK_SZ);
			}

			// assume sent block will be ACK'd
			genBlk(blkBuf); // prepare next block
			// assume sent block was ACK'd
		};
		// finish up the protocol, assuming the receiver behaves normally
		// ********* fill in some code here ***********
		sendByte(EOT);
		sendByte(EOT);


		//(myClose(transferringFileD));
		if (-1 == myClose(transferringFileD))
			ErrorPrinter("myClose(transferringFileD)", __FILE__, __LINE__, errno);
		result = "Done";
	}
}

