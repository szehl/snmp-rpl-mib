/*! \file avr_xram.c ************************************************************
 *
 * \brief
 *      enables external SRAM usage for RZRAVEN USB Stick AT90USB1287
 * 		written for 32KByte external RAM
 *
 * \addtogroup usbstick
 *
 * \author
 *      Sven Zehl <svenzehl@web.de>
 *
 ******************************************************************************/
 
#include "avr-xram.h"

/**
 \brief enable XMEM interface
 */
void xram_enable(void)
{
	
	XMCRA = ((1 << SRE));  	//enable XMEM interface with 0 wait states, switch Bit 7 of XMCRA on to enable XMEM usage
	XMCRB = 0;			 	//use all Port C Pins for XRAM Addresses, Port C7 no needed, 
							//but we don't need it till it is not connected to anything
}

