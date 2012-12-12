#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "mib-init.h"
#include "ber.h"
#include "utils.h"
#include "logging.h"
#include "dispatcher.h"
#include "radio.h"
#include "rf230bb.h"
#include <avr/io.h>
#include "snmpd.h"
#include "snmpd-types.h"

//RPL MIB includes
#include "sicslowpan.h"
#include "net/rpl/rpl-private.h"
#include "net/rpl/rpl.h"
#include "net/uip-ds6.h"

#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#define AVR_SNMP 1
//end RPL MIB includes

#if (CONTIKI_TARGET_AVR_RAVEN || CONTIKI_TARGET_AVR_ZIGBIT) && ENABLE_PROGMEM
#include <avr/pgmspace.h>
#else
#define PROGMEM
#endif

/*sz*/
/*Enable Printf() Debugging*/
/** \brief Enable Printf() Debugging*/
#define PDEBUG 0
/*sz*/

//RPL MIB help functions

#define RplInstanceTableSize RPL_MAX_INSTANCES
#define rplInstanceDISMode         2
#define rplInstanceDISMessages     3
#define rplInstanceDISTimeout      4
#define rplInstanceModeOfOperation 5

u32t EncodeTableOID(u8t* ptr, u32t pos, u32t value)
{
  switch(ber_encoded_oid_item_length(value)){
  case 5:
    ptr[pos] = ((value >> (7 * 4)) & 0x7F) | 0x80;
    pos++;
  case 4:
    ptr[pos] = ((value >> (7 * 3)) & 0x7F) | 0x80;
    pos++;
  case 3:
    ptr[pos] = ((value >> (7 * 2)) & 0x7F) | 0x80;
    pos++;
  case 2:
    ptr[pos] = ((value >> (7 * 1)) & 0x7F) | 0x80;
    pos++;
  case 1:
    ptr[pos] = ((value >> (7 * 0)) & 0x7F);
    pos++;
    break;
  default:
    return -1;
  }
  return pos;
}

ptr_t* handleTableNextOid2(u8t* oid, u8t len, u8t* columns, u8t columnNumber, u8t rowNumber) {
  ptr_t* ret = 0;
  u32t oid_el1, oid_el2;
  u8t i;
  i = ber_decode_oid_item(oid, len, &oid_el1);
  i = ber_decode_oid_item(oid + i, len - i, &oid_el2);
  for (i = 0; i < columnNumber; i++) {
    if (oid_el1 < columns[i] || (oid_el1 == columns[i] && oid_el2 < rowNumber)) {
      ret = oid_create();
      CHECK_PTR_U(ret);
      ret->len = 2;
      ret->ptr = malloc(2);
      CHECK_PTR_U(ret->ptr);
      ret->ptr[0] = columns[i];
      if (oid_el1 < columns[i]) {
	ret->ptr[1] = 1;
      } else {
	ret->ptr[1] = oid_el2 + 1;
      }
      break;
    }
  }
  return ret;
}

u8t RplInstanceColumns[] = {rplInstanceDISMode, rplInstanceDISMessages, rplInstanceDISTimeout, rplInstanceModeOfOperation};


s8t instance_loc(u32t oid_instance_id) {
  extern rpl_instance_t instance_table[];
  int i;

  for (i = 0; i < RplInstanceTableSize; i++) {
    if (instance_table[i].instance_id == oid_instance_id) {
      return i+1;
    }
  }
  return -1;
}

ptr_t* getNextOIDRplInstanceEntry(mib_object_t* object, u8t* oid, u8t len) {
  ptr_t* ret = 0;
  u32t oid_el1, oid_el2;
  u8t i;
  u8t columnNumber = 4;
  
  extern rpl_instance_t instance_table[];
  
  i = ber_decode_oid_item(oid, len, &oid_el1);
  i = ber_decode_oid_item(oid + i, len - i, &oid_el2);

  for(i = 0; i < columnNumber; i++) {
    if (oid_el1 < RplInstanceColumns[i] || 
	(oid_el1 == RplInstanceColumns[i] && (instance_loc(oid_el2) < RplInstanceTableSize || instance_loc(oid_el2) == -1))) {
      ret = oid_create();
      CHECK_PTR_U(ret);
      ret->len = 2;
      ret->ptr = malloc(2);
      CHECK_PTR_U(ret->ptr);
      ret->ptr[0] = RplInstanceColumns[i];
      if (oid_el1 < RplInstanceColumns[i] || instance_loc(oid_el2) == -1) {
	ret->ptr[1] = instance_table[0].instance_id;
      } else {
	if(instance_loc(oid_el2) < RplInstanceTableSize) {
	  ret->ptr[1] =  instance_table[instance_loc(oid_el2)].instance_id;	  
	} else {
	  return 0;
	}	
      }
      break;
    }
  }
  return ret;
}

//end RPL MIB end function

/* common oid prefixes*/
static u8t ber_oid_system_desc[] PROGMEM  				= {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00};
static ptr_t oid_system_desc PROGMEM      				= {ber_oid_system_desc, 8};
static u8t ber_oid_system_time[] PROGMEM  				= {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00};
static ptr_t oid_system_time PROGMEM      				= {ber_oid_system_time, 8};

static u8t ber_oid_system_sysContact [] PROGMEM         = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x04, 0x00};
static ptr_t oid_system_sysContact PROGMEM              = {ber_oid_system_sysContact, 8};
static u8t ber_oid_system_sysName [] PROGMEM            = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x05, 0x00};
static ptr_t oid_system_sysName PROGMEM                 = {ber_oid_system_sysName, 8};
static u8t ber_oid_system_sysLocation [] PROGMEM        = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x06, 0x00};
static ptr_t oid_system_sysLocation PROGMEM             = {ber_oid_system_sysLocation, 8};

static u8t ber_oid_system_str[] PROGMEM   				= {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x0B, 0x00};
static ptr_t oid_system_str PROGMEM       				= {ber_oid_system_str, 8};
static u8t ber_oid_system_tick[] PROGMEM  				= {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x0D, 0x00};
static ptr_t oid_system_tick PROGMEM      				= {ber_oid_system_tick, 8};

/* SNMP group */
static u8t ber_oid_snmpInPkts[] PROGMEM                 = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x0B, 0x01, 0x00};
static ptr_t oid_snmpInPkts PROGMEM                     = {ber_oid_snmpInPkts, 8};
static u8t ber_oid_snmpInBadVersions[] PROGMEM          = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x0B, 0x03, 0x00};
static ptr_t oid_snmpInBadVersions PROGMEM              = {ber_oid_snmpInBadVersions, 8};
static u8t ber_oid_snmpInASNParseErrs[] PROGMEM         = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x0B, 0x06, 0x00};
static ptr_t oid_snmpInASNParseErrs PROGMEM             = {ber_oid_snmpInASNParseErrs, 8};


/* Beuth Socket*/
static u8t ber_oid_steckdose_int[] PROGMEM     			= {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0xac, 0x5d, 0x64, 0x84, 0x58, 0x01, 0x00};
static ptr_t oid_steckdose_int PROGMEM         			= {ber_oid_steckdose_int, 13};

/*RSSI Value*/
static u8t ber_oid_rssi_int[] PROGMEM     				= {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0xac, 0x5d, 0x64, 0x84, 0x58, 0x02, 0x00};
static ptr_t oid_rssi_int PROGMEM         				= {ber_oid_rssi_int, 13};

/*Temp Value*/
static u8t ber_oid_temp_int[] PROGMEM     				= {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0xac, 0x5d, 0x64, 0x84, 0x58, 0x03, 0x00};
static ptr_t oid_temp_int PROGMEM         				= {ber_oid_temp_int, 13};

s8t getSysDescr(mib_object_t* object, u8t* oid, u8t len)
{
    if (!object->varbind.value.p_value.len) {
        object->varbind.value.p_value.ptr = (u8t*)"Socket, Remote Controlled via 6LoWPAN and SNMPv3, Running Contiki OS 2.5 on AVR Zigbit";
        object->varbind.value.p_value.len = 54;
    }
    return 0;
}

s8t setSysDescr(mib_object_t* object, u8t* oid, u8t len, varbind_value_t value)
{
    object->varbind.value.p_value.ptr = (u8t*)"6LoWPAN SNMP Socket";
    object->varbind.value.p_value.len = 19;
    return 0;
}

s8t getTimeTicks(mib_object_t* object, u8t* oid, u8t len)
{
    object->varbind.value.u_value = getSysUpTime();
#if PDEBUG
	printf("aktuelle Zeit mit clock time: %d",clock_time());
	printf("aktuelle Zeit mit getSysUpTime: %d",getSysUpTime());
#endif
    return 0;
}

/* 110620 fschw: add Steckdose for snmpd - start */
/** \brief 6LoWPAN Socket get function*/
s8t getBeuthState(mib_object_t* object, u8t* oid, u8t len)
{
    object->varbind.value.i_value = ((PORTG >> PIN2) & 1);
#if PDEBUG
	printf("Get Pin State ausgeführt, Ergebnis %d\n",object->varbind.value.i_value);
#endif
    return 0;
}

/** \brief 6LoWPAN Socket set function*/
s8t setBeuthState(mib_object_t* object, u8t* oid, u8t len, varbind_value_t value)
{
	DDRG |= (1 << PIN2);
	DDRG |= (1 << PIN1);
    if (value.i_value == 1) {
        PORTG |= (1 << PIN2);
		PORTG |= (1 << PIN1);
    } else {
        PORTG &= ~(1 << PIN2);
		PORTG &= ~(1 << PIN1);
    }
#if PDEBUG
	printf("Set Pin: Pin7 hat nun den Zustand:%d\n",object->varbind.value.i_value);
#endif
	return 0;
}
/* 110620 fschw end */

/* RSSI value*/
/** \brief RSSI value get function*/
s8t getRssiValue(mib_object_t* object, u8t* oid, u8t len)
{
	int rssi_temp;
	rssi_temp=rf230_get_raw_rssi();
    object->varbind.value.i_value = (-91)+(rssi_temp); //Already multiplicated with three
#if PDEBUG
	printf("Get RSSI Value ausgeführt, iValue Ergebnis %d\n",object->varbind.value.i_value);
#endif
    return 0;
}

/* RSSI value end */

/* temperature value*/
/************************/
/** \brief function for initialization of the adc*/
void adc_init()
{
    // AREF = AVcc
    ADMUX = (1<<REFS0);

    // ADC Enable and prescaler of 128
    // 16000000/128 = 125000
    ADCSRA = (1<<ADEN)|(1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0);
}

/** \brief adc read function */
u16t adc_read()
{
    ADMUX = (ADMUX & 0xF8)|3;     // clears the bottom 3 bits before ORing
    // start single convertion
    // write '1' to ADSC
    ADCSRA |= (1<<ADSC);
    // wait for conversion to complete
    // ADSC becomes '0' again
    // till then, run loop continuously
    while(ADCSRA & (1<<ADSC));

    return (ADC);
}

/** \brief array for comparision of  the temperature value*/
u16t temp_array[] PROGMEM ={325, 311, 296, 282, 267,
                           253, 242, 231, 220, 209,
				           198, 189, 182, 173, 165,
				           157, 151, 144, 138, 131,
				           125, 120, 115, 110, 105,
				           100, 96, 92, 88, 84,
				           80, 77, 74, 71, 68,
				           65, 63, 60, 58, 55,
				           53, 51, 49, 47, 45,
				           43, 42, 40, 39, 37,
				           36, 35, 33, 32, 30,
				           29, 28, 27, 27, 26,
				           25, 24, 23, 23, 22,
				           21, 20, 19, 19, 18,
				           17, 17, 16, 16, 15,
				           15, 14, 14, 13, 13,
				           12, 12, 12, 11, 11,
				           11, 11, 10, 10, 9,
				           9, 9, 8, 8, 7,
				           7, 7, 7, 6, 6,
				           6};
//Werte laut Datenblatt in 5grad Schritten Zwischenwerte gemittelt Beginn bei 0°

/** \brief function for reading the temperature arrray out of the programm memory*/
u16t getTempArray(u8t index) {
    return pgm_read_dword(&temp_array[index]);
}

/** \brief function for converting the adc value to the corresponding value in ohms*/
float adcToOhm(u16t adc_value)
{
	//float one_adc=0.00318359375;
	float one_adc=0.003076171875;
	float r_div=10000;
	float u_ges=3.15;
	float r_ntc;
	r_ntc=(((float)adc_value)*one_adc*r_div)/(u_ges-(((float)adc_value)*one_adc));
#if PDEBUG
	printf("RNTC=%f\n",r_ntc);
#endif
	return r_ntc;
}

/** \brief function for comparing between the ntc  value in ohms and the temperature array, returns coresponding temperature in celsius*/
u8t find_temp_celsius(float r_ntc)
{
	float rt_r25;
	float r_div=10000;
	rt_r25=(r_ntc/r_div)*100;
	u8t i=0;
	while((u16t)rt_r25 <= getTempArray(i))
	{
		i++;
	}
	return (i-1);
}

/** \brief get function for the temperature mib object*/
s8t getTempValue(mib_object_t* object, u8t* oid, u8t len)
{
	adc_init();
	u16t adc_value;
	//u8t adc_channel;
	float r_ntc;
	//adc_channel=3;
	//adc_value=adc_read(adc_channel);
	adc_value=adc_read();
	r_ntc=adcToOhm(adc_value);
    object->varbind.value.i_value = find_temp_celsius(r_ntc);
#if PDEBUG
	printf("Get temperature Value ausgeführt, ADC Value int %d RNTC=%f Ohm\n",adc_value,r_ntc);
	printf("Get temperature Value ausgeführt, iValue Ergebnis in Grad Celsius: %d\n",object->varbind.value.i_value);
#endif
    return 0;
}

/* temperature value end */


/*
 * SNMP group
 */
s8t getMIBSnmpInPkts(mib_object_t* object, u8t* oid, u8t len)
{
    object->varbind.value.u_value = getSnmpInPkts();
#if PDEBUG
	printf("Get InPakets.value = %d\n Get InPakets.function=%d \n",object->varbind.value.u_value, getSnmpInPkts());
#endif
    return 0;
}

s8t getMIBSnmpInBadVersions(mib_object_t* object, u8t* oid, u8t len)
{
    object->varbind.value.u_value = getSnmpInBadVersions();
    return 0;
}


s8t getMIBSnmpInASNParseErrs(mib_object_t* object, u8t* oid, u8t len)
{
    object->varbind.value.u_value = getSnmpInASNParseErrs();
    return 0;
}

//RPL MIB DODAG PARENT AND CHILD TABLE

/*
 * rplDodagParentTable group
 *
 * rplDodagParentTable(6)
 *  +-rplDodagParentEntry(1) [rplInstanceID,rplDodagIndex,rplDodagParentID]
 *     +- --- InetAddressIPv6 rplDodagParentID(1)
 *     +- r-n InterfaceIndex  rplDodagParentIf(2)
 */

/* rplDodagParentTable original */

/*
static const u8t ber_oid_rplDodagParentEntry[] PROGMEM = {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0xF2, 0x06, 0x01, 0x02, 0x01, 0x06, 0x01};
static const ptr_t oid_rplDodagParentEntry PROGMEM     = {ber_oid_rplDodagParentEntry, 13};

#define rplDodagParentIf 2

s8t getRplDodagParentEntry(mib_object_t* object, u8t* oid, u8t len) {
  u32t oid_el1, oid_el2, oid_el3, oid_el4;
  u8t i=0, j=0;
  u8t searchid[16];
  rpl_instance_t *instance;
  rpl_parent_t *currentparent;

  if (len < 19) {
    return -1;
  }

  i = ber_decode_oid_item(oid, len, &oid_el1);

  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el2);
  instance = rpl_get_instance(oid_el2);
  if (instance == NULL) {
    return -1;
  }

  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el3);
  if(!instance->dag_table[oid_el3-1].used || oid_el3 > RPL_MAX_DAG_PER_INSTANCE) {
    return -1;
  }

  currentparent = list_head(instance->dag_table[oid_el3-1].parents);

  for (j = 0; j < 16; j++) {
    i = i + ber_decode_oid_item(oid + i, len - i, &oid_el4);
    searchid[j] = oid_el4;
  }

  for (j = 0; j < 16; j++) {
    if (searchid[j] != currentparent->addr.u8[j]) {
      if (currentparent->next == NULL) {
	return -1;
      } else {
	currentparent = currentparent->next;
	j = 0;
      }
    }
  }

  switch(oid_el1)
    {
    case rplDodagParentIf:
      object->varbind.value_type = BER_TYPE_INTEGER;
      object->varbind.value.i_value = 1;
      break;
    default:
      return -1;
    }  
  return 0;
}

u8t RplDodagParentColumns[] = {rplDodagParentIf};

ptr_t* getNextOIDRplDodagParentEntry(mib_object_t* object, u8t* oid, u8t len) {
  ptr_t* ret = 0;
  u32t oid_el1, oid_el2, oid_el3, oid_el4;
  u32t oid_n1, oid_n2, oid_n3;
  u8t i, j, parentLoc, columnNumber = 1, searchid[16];
  rpl_parent_t *currentparent;
  u32t leng, pos;
  
  extern rpl_instance_t instance_table[];
  
  i = ber_decode_oid_item(oid, len, &oid_el1);
  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el2);
  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el3);

  if (instance_table[0].dag_table[0].parents == NULL) {
    return 0;
  }
  currentparent = list_head(instance_table[0].dag_table[0].parents);

  if(oid_el1 <= 0 || (instance_loc(oid_el2) == -1 && oid_el2 < instance_table[0].instance_id)) {
    // rplDodagParentIf, rplInstanceID, rplDodagIndex 
    leng = ber_encoded_oid_item_length(RplDodagParentColumns[0]);
    leng = leng + ber_encoded_oid_item_length(instance_table[0].instance_id);
    leng = leng + ber_encoded_oid_item_length(1);

    // rplDodagParentID 
    for (j = 0; j < 16; j++) {
      leng = leng + ber_encoded_oid_item_length(currentparent->addr.u8[j]);
    }

    ret = oid_create();
    CHECK_PTR_U(ret);
    ret->len = leng;
    ret->ptr = malloc(leng);
    CHECK_PTR_U(ret->ptr);
    pos = 0;


    // Encoding: rplDodagParentIf, rplInstanceID, rplDodagIndex 
    pos = EncodeTableOID(ret->ptr, pos, RplDodagParentColumns[0]);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, instance_table[0].instance_id);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, 1);
    if (pos == -1) {
      return 0;
    }
    // Encoding: rplDodagParentID 
    for (j = 0; j < 16; j++) {
      pos = EncodeTableOID(ret->ptr, pos, currentparent->addr.u8[j]);
      if (pos == -1) {
        return 0;
      }
    }
  } else {
    // rplDodagParentIf, rplInstanceID, rplDodagIndex 
    leng = ber_encoded_oid_item_length(RplDodagParentColumns[0]);
    leng = leng + ber_encoded_oid_item_length(instance_table[0].instance_id);
    leng = leng + ber_encoded_oid_item_length(1);
    
    // rplDodagParentID 
    for (j = 0; j < 16; j++) {
      i = i + ber_decode_oid_item(oid, len, &oid_el4);
      searchid[j] = *( u8t* ) &oid_el4;
    }

    if(oid_el2 < instance_table[0].instance_id && oid_el3 < 1) {
      for (j = 0; j < 16; j++) {
	leng = leng + ber_encoded_oid_item_length(currentparent->addr.u8[j]);
      }      
    } else {    
      while(currentparent->next != NULL) {
	for (j = 0; j < 16; j++) {
	  if(currentparent->addr.u8[j] != searchid[j]) {
	    break;
	  }
	}
	if (j != 16) {
	  currentparent = currentparent->next;
	} else {
	  for (j = 0; j < 16; j++) {
	    leng = leng + ber_encoded_oid_item_length(currentparent->addr.u8[j]);
	  }
	  break;
	}
      }
    }

    if(leng < 19) {
      return 0;
    }
    
    ret = oid_create();
    CHECK_PTR_U(ret);
    ret->len = leng;
    ret->ptr = malloc(leng);
    CHECK_PTR_U(ret->ptr);
    pos = 0;
    
    // Encoding: rplDodagParentIf, rplInstanceID, rplDodagIndex 
    pos = EncodeTableOID(ret->ptr, pos, RplDodagParentColumns[0]);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, instance_table[0].instance_id);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, 1);
    if (pos == -1) {
      return 0;
    }
    // Encoding: rplDodagParentID 
    for (j = 0; j < 16; j++) {
      pos = EncodeTableOID(ret->ptr, pos, currentparent->addr.u8[j]);
      if (pos == -1) {
        return 0;
      } 
    }
  }     
  return ret;
}
*/

/* rplDodagParentTable preferred parent hack */

static const u8t ber_oid_rplDodagParentEntry[] PROGMEM = {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0xF2, 0x06, 0x01, 0x02, 0x01, 0x06, 0x01};
static const ptr_t oid_rplDodagParentEntry PROGMEM     = {ber_oid_rplDodagParentEntry, 13};

#define rplDodagParentIf 2

s8t getRplDodagParentEntry(mib_object_t* object, u8t* oid, u8t len) {
  u32t oid_el1, oid_el2, oid_el3, oid_el4;
  u8t i=0, j=0;
  u8t searchid[16];
  rpl_instance_t *instance;
  rpl_parent_t *currentparent;
  
  /*sz*/ 
  /*To replace  neighbor table with preferred parent*/
  /*This is not really the best solution, but it works*/
  rpl_dag_t *dag;
  dag = rpl_get_any_dag();
 
  PRINTF("RPL: The preferred parent is ");
  PRINT6ADDR(&dag->preferred_parent->addr);
  PRINTF("\n");
  /*sz*/


  if (len < 19) {
    return -1;
  }

  i = ber_decode_oid_item(oid, len, &oid_el1);

  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el2);
  instance = rpl_get_instance(oid_el2);
  if (instance == NULL) {
    return -1;
  }

  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el3);
  if(!instance->dag_table[oid_el3-1].used || oid_el3 > RPL_MAX_DAG_PER_INSTANCE) {
    return -1;
  }

  currentparent = list_head(instance->dag_table[oid_el3-1].parents);

  for (j = 0; j < 16; j++) {
    i = i + ber_decode_oid_item(oid + i, len - i, &oid_el4);
    searchid[j] = oid_el4;
  }

  for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
    if (searchid[j] != dag->preferred_parent->addr.u8[j]) {
	//if (searchid[j] != currentparent->addr.u8[j]) {
/*sz*/
      if (currentparent->next == NULL) {
	return -1;
      } else {
	currentparent = currentparent->next;
	j = 0;
      }
    }
  }

  switch(oid_el1)
    {
    case rplDodagParentIf:
      object->varbind.value_type = BER_TYPE_INTEGER;
      object->varbind.value.i_value = 1;
      break;
    default:
      return -1;
    }  
  return 0;
}

u8t RplDodagParentColumns[] = {rplDodagParentIf};

ptr_t* getNextOIDRplDodagParentEntry(mib_object_t* object, u8t* oid, u8t len) {
  ptr_t* ret = 0;
  u32t oid_el1, oid_el2, oid_el3, oid_el4;
  u32t oid_n1, oid_n2, oid_n3;
  u8t i, j, parentLoc, columnNumber = 1, searchid[16];
  rpl_parent_t *currentparent;
  u32t leng, pos;
  
  extern rpl_instance_t instance_table[];
  
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
  rpl_dag_t *dag;
  dag = rpl_get_any_dag();
  
  //PRINTF("RPL: The preferred parent is ");
  //PRINT6ADDR(&dag->preferred_parent->addr);
/*sz*/
  
  i = ber_decode_oid_item(oid, len, &oid_el1);
  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el2);
  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el3);

  if (instance_table[0].dag_table[0].parents == NULL) {
    return 0;
  }
  currentparent = list_head(instance_table[0].dag_table[0].parents);

  if(oid_el1 <= 0 || (instance_loc(oid_el2) == -1 && oid_el2 < instance_table[0].instance_id)) {
    /* rplDodagParentIf, rplInstanceID, rplDodagIndex */
    leng = ber_encoded_oid_item_length(RplDodagParentColumns[0]);
    leng = leng + ber_encoded_oid_item_length(instance_table[0].instance_id);
    leng = leng + ber_encoded_oid_item_length(1);

    /* rplDodagParentID */
    for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
	  //leng = leng + ber_encoded_oid_item_length(currentparent->addr.u8[j]);
      leng = leng + ber_encoded_oid_item_length(dag->preferred_parent->addr.u8[j]);
/*sz*/
    }

    ret = oid_create();
    CHECK_PTR_U(ret);
    ret->len = leng;
    ret->ptr = malloc(leng);
    CHECK_PTR_U(ret->ptr);
    pos = 0;


    /* Encoding: rplDodagParentIf, rplInstanceID, rplDodagIndex */
    pos = EncodeTableOID(ret->ptr, pos, RplDodagParentColumns[0]);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, instance_table[0].instance_id);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, 1);
    if (pos == -1) {
      return 0;
    }
    /* Encoding: rplDodagParentID */
    for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
      //pos = EncodeTableOID(ret->ptr, pos, currentparent->addr.u8[j]);
      pos = EncodeTableOID(ret->ptr, pos, dag->preferred_parent->addr.u8[j]);
/*sz*/
      if (pos == -1) {
        return 0;
      }
    }
  } else {
    /* rplDodagParentIf, rplInstanceID, rplDodagIndex */
    leng = ber_encoded_oid_item_length(RplDodagParentColumns[0]);
    leng = leng + ber_encoded_oid_item_length(instance_table[0].instance_id);
    leng = leng + ber_encoded_oid_item_length(1);
    
    /* rplDodagParentID */
    for (j = 0; j < 16; j++) {
      i = i + ber_decode_oid_item(oid, len, &oid_el4);
      searchid[j] = *( u8t* ) &oid_el4;
    }

    if(oid_el2 < instance_table[0].instance_id && oid_el3 < 1) {
      for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
    //leng = leng + ber_encoded_oid_item_length(currentparent->addr.u8[j]);
	leng = leng + ber_encoded_oid_item_length(dag->preferred_parent->addr.u8[j]);
/*sz*/
      }      
    } else {    
      while(currentparent->next != NULL) {
	for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
      //if(currentparent->addr.u8[j] != searchid[j]) {
	  if(dag->preferred_parent->addr.u8[j] != searchid[j]) {
/*sz*/
	    break;
	  }
	}
	if (j != 16) {
	  currentparent = currentparent->next;
	} else {
	  for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
        //leng = leng + ber_encoded_oid_item_length(currentparent->addr.u8[j]);
	    leng = leng + ber_encoded_oid_item_length(dag->preferred_parent->addr.u8[j]);
/*sz*/
	  }
	  break;
	}
      }
    }

    if(leng < 19) {
      return 0;
    }
    
    ret = oid_create();
    CHECK_PTR_U(ret);
    ret->len = leng;
    ret->ptr = malloc(leng);
    CHECK_PTR_U(ret->ptr);
    pos = 0;
    
    /* Encoding: rplDodagParentIf, rplInstanceID, rplDodagIndex */
    pos = EncodeTableOID(ret->ptr, pos, RplDodagParentColumns[0]);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, instance_table[0].instance_id);
    if (pos == -1) {
      return 0;
    }
    pos = EncodeTableOID(ret->ptr, pos, 1);
    if (pos == -1) {
      return 0;
    }
    /* Encoding: rplDodagParentID */
    for (j = 0; j < 16; j++) {
/*sz*/ 
/*To replace  neighbor table with preferred parent*/
      //pos = EncodeTableOID(ret->ptr, pos, currentparent->addr.u8[j]);
      pos = EncodeTableOID(ret->ptr, pos, dag->preferred_parent->addr.u8[j]);
/*sz*/
      if (pos == -1) {
        return 0;
      } 
    }
  }     
  return ret;
}






/*
 * rplDodagChildTable group
 *
 * rplDodagChildTable(7)
 *  +-rplDodagChildEntry(1) [rplInstanceID,rplDodagIndex,
 *     |                      rplDodagChildID]
 *     +- --- InetAddressIPv6 rplDodagChildID(1)
 *     +- r-n InterfaceIndex  rplDodagChildIf(2)
 */

/* rplDodagChildTable */
/*
static const u8t ber_oid_rplDodagChildEntry[] PROGMEM = {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0xF2, 0x06, 0x01, 0x02, 0x01, 0x07, 0x01};
static const ptr_t oid_rplDodagChildEntry PROGMEM     = {ber_oid_rplDodagChildEntry, 13};

#define rplDodagChildIf 2

s8t loc_route(u8t *searchid, rpl_dag_t *dag) {
  int i, j;
  extern uip_ds6_route_t uip_ds6_routing_table[];

  for (i = 0; i < UIP_DS6_ROUTE_NB; i++) {
    for (j = 0; j < 16; j++) {
      if (searchid[j] != uip_ds6_routing_table[i].nexthop.u8[j])
	break;
    }
    if (j == 16 && uip_ds6_routing_table[i].isused && uip_ds6_routing_table[i].state.dag == dag) {
      return i;
    }
  }
  return -1;
}

s8t getRplDodagChildEntry(mib_object_t* object, u8t* oid, u8t len) {
  u32t oid_el1, oid_el2, oid_el3, oid_el4;
  u8t i=0, j=0, pos=0;
  u8t searchid[16];
  rpl_instance_t *instance;
  extern uip_ds6_route_t uip_ds6_routing_table[];

  if (len < 19) {
    return -1;
  }

  i = ber_decode_oid_item(oid, len, &oid_el1);

  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el2);
  instance = rpl_get_instance(oid_el2);
  if (instance == NULL) {
    return -1;
  }

  i = i + ber_decode_oid_item(oid + i, len - i, &oid_el3);
  if(!instance->dag_table[oid_el3-1].used || oid_el3 > RPL_MAX_DAG_PER_INSTANCE) {
    return -1;
  }

  // search for child routes 
  for (j = 0; j < 16; j++) {
    i = i + ber_decode_oid_item(oid + i, len - i, &oid_el4);
    searchid[j] = *( u8t* ) &oid_el4;
  }
  
  pos = loc_route(searchid, &instance->dag_table[oid_el3-1]);
  if (pos == -1) {
    return -1;
  }
  
  switch(oid_el1)
    {
    case rplDodagParentIf:
      object->varbind.value_type = BER_TYPE_INTEGER;
      object->varbind.value.i_value = 1;
      break;
    default:
      return -1;
    }  
  return 0;
}
*/
//end RPL MIB CHILD PARENT TABLE



/*


/*-----------------------------------------------------------------------------------*/
/*
 * Initialize the MIB.
 */
s8t mib_init()
{


    if (add_scalar(&oid_system_desc, FLAG_ACCESS_READONLY, BER_TYPE_OCTET_STRING, 0, &getSysDescr, &setSysDescr) == -1 ||
        add_scalar(&oid_system_time, FLAG_ACCESS_READONLY, BER_TYPE_TIME_TICKS, 0, &getTimeTicks, 0) == -1  ||

		add_scalar(&oid_system_sysContact, FLAG_ACCESS_READONLY, BER_TYPE_OCTET_STRING, "<szehl@beuth-hochschule.de>", 0, 0) == -1 ||
        add_scalar(&oid_system_sysName, 0, BER_TYPE_OCTET_STRING, "6LoWPAN SNMPv3 Socket", 0, 0) == -1 ||
        add_scalar(&oid_system_sysLocation, 0, BER_TYPE_OCTET_STRING, "Beuth Hochschule Berlin - IPv6-Lab", 0, 0) == -1)

		{
        return -1;
    }

	// snmp group

	    if (add_scalar(&oid_snmpInPkts, FLAG_ACCESS_READONLY, BER_TYPE_COUNTER, 0, &getMIBSnmpInPkts, 0) != ERR_NO_ERROR ||
        add_scalar(&oid_snmpInBadVersions, FLAG_ACCESS_READONLY, BER_TYPE_COUNTER, 0, &getMIBSnmpInBadVersions, 0) != ERR_NO_ERROR ||
        add_scalar(&oid_snmpInASNParseErrs, FLAG_ACCESS_READONLY, BER_TYPE_COUNTER, 0, &getMIBSnmpInASNParseErrs, 0) != ERR_NO_ERROR) {
        return -1;
    }

	if (add_scalar(&oid_steckdose_int, 0, BER_TYPE_INTEGER, 0, &getBeuthState, &setBeuthState) == -1)
	{
        return -1;
    }
		if (add_scalar(&oid_rssi_int, FLAG_ACCESS_READONLY, BER_TYPE_INTEGER, 0, &getRssiValue, 0) == -1)
	{
        return -1;
    }

	if (add_scalar(&oid_temp_int, FLAG_ACCESS_READONLY, BER_TYPE_INTEGER, 0, &getTempValue, 0) == -1)
	{
        return -1;
    }
    
      // rplDodagParentTable group
  if (add_table(&oid_rplDodagParentEntry, &getRplDodagParentEntry, &getNextOIDRplDodagParentEntry, 0) == -1) {
    return -1;
  }
     //rplDodagChildTable group
  //if (add_table(&oid_rplDodagChildEntry, &getRplDodagChildEntry, 0, 0) == -1) {
  //  return -1;
 // }


    return 0;
}
