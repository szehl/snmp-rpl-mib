/*! \file avr_xram.h ************************************************************
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
 
#include "contiki.h"
 
#ifndef __AVR-XRAM_H
#define __AVR-XRAM_H

void xram_enable(void);     

#endif

