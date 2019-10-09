/***************************************************************************//**
 * @file
 * @brief	RFID Reader
 * @author	Ralf Gerhauser
 * @version	2018-10-10
 *
 * This module provides the functionality to communicate with the @ref
 * RFID_Reader.
 * It contains the following parts:
 * - Initialize functionality according to the configuration variables.
 * - Power management for RFID reader and UART
 * - UART driver to receive data from the RFID reader
 * - Decoders to handle the received data for Short and Long Range readers
 * - When the "Absence Detection" is configured, disabling the RFID reader
 *   is deferred as long as a transponder is still present.
 *
 ****************************************************************************//*
 *
 * Parts are Copyright 2013 Energy Micro AS, http://www.energymicro.com
 *
 *******************************************************************************
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 * 4. The source and compiled code may only be used on Energy Micro "EFM32"
 *    microcontrollers and "EFR4" radios.
 *
 * DISCLAIMER OF WARRANTY/LIMITATION OF REMEDIES: Energy Micro AS has no
 * obligation to support this Software. Energy Micro AS is providing the
 * Software "AS IS", with no express or implied warranties of any kind,
 * including, but not limited to, any implied warranties of merchantability
 * or fitness for any particular purpose or warranties against infringement
 * of any proprietary rights of a third party.
 *
 * Energy Micro AS will not be liable for any consequential, incidental, or
 * special damages, or any other relief, or for any claim by any third party,
 * arising from your use of this Software.
 *
 ****************************************************************************//*
Revision History:
2019-02-10,rage	- BugFix: Absent detection didn't work if transponder ID has
		  been read just once before disappearing again.
		- RFID_Decode: Put generic parts at the end of the routine,
		  added debug code to print the received data bytes.
2018-12-06,rage	- Changed decoding for LR reader to be compatible with the RFID
		  handheld reader.
2018-10-10,rage	- When the "Absence Detection" is configured, disabling the RFID
		  reader is deferred as long as a transponder is still present.
2018-03-26,rage	- Reduced code to support one UART only.
		- Include code which is light-barrier related only if define
		  RFID_TRIGGERED_BY_LIGHT_BARRIER is 1.
		- Implemented new feature "Absence Detection" which reports
		  when a transponder is no more detected (read) after some time.
		- This module is completely configurable via CONFIG.TXT file.
		- Added IsRFID_Active() and IsRFID_Enabled().
2017-06-20,rage	- Separated USART settings from RFID settings to be able to
		  support different types of RFID readers.
		- Implemented decoding of Long Range Reader protocol, including
		  CRC calculation and verification.
2017-05-02,rage	- RFID_Init: Added RFID_PWR_OUT structure to specify the
		  power outputs for the two RFID readers.
		- RFID_Parms contains all relevant parameters to initialize
		  the related UART.
2017-04-09,rage	- Extended code to support two RFID readers.
		- Removed Tx part of UART code because it was never used.
2016-04-05,rage	Made all local variables of type "volatile".
2014-11-25,rage	Initial version.
*/

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <string.h>
#include "em_device.h"
#include "em_assert.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"
#include "AlarmClock.h"
#include "DisplayMenu.h"
#include "RFID.h"
#include "Logging.h"
#include "Control.h"

/*=============================== Definitions ================================*/

    // Module Debugging
#define MOD_DEBUG	0	// set 1 to enable debugging of this module
#if ! MOD_DEBUG
    #undef  DBG_PUTC
    #undef  DBG_PUTS
    #define DBG_PUTC(ch)	// define as empty
    #define DBG_PUTS(str)
#endif

/*=========================== Typedefs and Structs ===========================*/

/*!@brief Structure to hold UART specific parameters */
typedef struct
{
    USART_TypeDef *	const	UART;		//!< UART device to use
    CMU_Clock_TypeDef	const	cmuClock_UART;	//!< CMU clock for the UART
    IRQn_Type		const	UART_Rx_IRQn;	//!< Rx interrupt number
    GPIO_Port_TypeDef	const	UART_Rx_Port;	//!< Port for RX pin
    uint32_t		const	UART_Rx_Pin;	//!< Rx pin on this port
    uint32_t		const	UART_Route;	//!< Route location
} USART_Parms;

/*!@brief Structure to hold RFID reader type specific parameters. */
typedef struct
{
    uint32_t		   const Baudrate;	//!< Baudrate for RFID reader
    USART_Databits_TypeDef const DataBits;	//!< Number of data bits
    USART_Parity_TypeDef   const Parity;	//!< Parity mode
    USART_Stopbits_TypeDef const StopBits;	//!< Number of stop bits
} RFID_TYPE_PARMS;

/*========================= Global Data and Routines =========================*/

    /*!@brief RFID reader type. */
RFID_TYPE g_RFID_Type;

    /*!@brief RFID reader type. */
PWR_OUT   g_RFID_Power;

    /*!@brief Duration in [s] after which a transponder is treated as absent */
uint32_t  g_RFID_AbsentDetectTimeout;

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /*!@brief Duration in [s] after which the RFID reader is powered-off. */
int32_t  g_RFID_PwrOffTimeout = DFLT_RFID_POWER_OFF_TIMEOUT;

    /*!@brief Duration in [s] during the RFID reader tries to read an ID. */
int32_t  g_RFID_DetectTimeout = DFLT_RFID_DETECT_TIMEOUT;
#endif

    /*!@brief Framing and Parity error counters of the USARTs. */
uint16_t g_FERR_Cnt;
uint16_t g_PERR_Cnt;

    /*!@brief Enum names for the RFID type. */
const char *g_enum_RFID_Type[] = { "SR", "LR", NULL };

    /*!@brief Transponder number */
char	 g_Transponder[18];

/*================================ Local Data ================================*/

    /*!@brief Flag that determines if RFID reader is in use. */
bool	 l_flgRFID_Activate;

    /*! Local structure to RFID type and power output configuration */
static RFID_CONFIG l_pRFID_Cfg;

    /*! RFID Reader specific parameters.  Entries are addresses via
     * enums @ref RFID_TYPE.
     */
static const RFID_TYPE_PARMS l_RFID_Type_Parms[NUM_RFID_TYPE] =
{
   {	// RFID_TYPE_SR - 0: Short Range RFID reader
	  9600,  usartDatabits8,  usartEvenParity,  usartStopbits1
   },
   {	// RFID_TYPE_LR - 1: Long Range RFID reader
	 38400,  usartDatabits8,  usartNoParity,    usartStopbits1
   }
};

    /*! USART specific parameters for each RFID reader */
static const USART_Parms l_USART_Parms =
{
    USART1, cmuClock_USART1, USART1_RX_IRQn,
    gpioPortC,  1, USART_ROUTE_LOCATION_LOC0
};

    /*! Flag if RFID reader should be powered on. */
static volatile bool	l_flgRFID_On;

    /*! Flag if RFID reader is currently powered on. */
static volatile bool	l_flgRFID_IsOn;

    /*! Timer handle for transponder absence detection. */
static volatile TIM_HDL	l_hdlRFID_AbsentDetect = NONE;

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /*! Timer handle for switching the RFID reader off after a time. */
static volatile TIM_HDL	l_hdlRFID_Off = NONE;

    /*! Flag indicates if an object is present. */
static volatile bool	l_flgObjectPresent;

    /*! Timer handler for the ID detection timeout */
static volatile TIM_HDL	l_hdlRFID_DetectTimeout = NONE;
#endif

    /*! Flag if a new run has been started, i.e. the module is prepared to
     *  receive a transponder number. */
static volatile bool	l_flgNewRun;

    /*! Flag to notify a new transponder ID */
static volatile bool	l_flgNewID;

    /*! State (index) variables for RFID_Decode. */
static volatile uint8_t	l_State;

/*=========================== Forward Declarations ===========================*/

static void TransponderAbsent(TIM_HDL hdl);

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
static void SwitchRFID_Off(TIM_HDL hdl);
static void RFID_DetectTimeout(TIM_HDL hdl);
#endif
static void uartSetup(void);


/***************************************************************************//**
 *
 * @brief	Initialize the RFID Reader frame work
 *
 * This routine initializes the @ref RFID_Reader and all the required
 * functionality according to the configuration variables @ref RFID_TYPE,
 * @ref RFID_POWER, and @ref RFID_ABSENT_DETECT_TIMEOUT.
 *
 ******************************************************************************/
void	RFID_Init (void)
{
    /* Check if RFID reader is already in use */
    if (l_flgRFID_Activate)
	RFID_PowerOff();	// power-off and reset reader and UART

    /* Now the RFID reader isn't active any more */
    l_flgRFID_Activate = false;

    if (g_RFID_Type == RFID_TYPE_NONE  ||  g_RFID_Power == PWR_OUT_NONE)
	return;

    /* Build new structure based on the configuration variables */
    l_pRFID_Cfg.RFID_Type   = g_RFID_Type;
    l_pRFID_Cfg.RFID_PwrOut = g_RFID_Power;

    /* RFID reader should be activated */
    l_flgRFID_Activate = true;

#ifdef LOGGING
    Log ("Initializing RFID reader of type %s for Power Output %s",
	 g_enum_RFID_Type[g_RFID_Type], g_enum_PowerOutput[g_RFID_Power]);
#endif

    if (g_RFID_AbsentDetectTimeout > 0)
    {
#ifdef LOGGING
	Log ("RFID reader Absent Detection is configured for %lds",
	     g_RFID_AbsentDetectTimeout);
#endif
	if (l_hdlRFID_AbsentDetect == NONE)
	    l_hdlRFID_AbsentDetect = sTimerCreate (TransponderAbsent);
    }
#ifdef LOGGING
    else
    {
	Log ("WARNING: RFID reader absence detection is disabled");
    }
#endif

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /* Get a timer handle to switch the RFID reader off after a time */
    if (l_hdlRFID_Off == NONE)
	l_hdlRFID_Off = sTimerCreate (SwitchRFID_Off);

    /* Create another timer for the ID detection timeout */
    if (l_hdlRFID_DetectTimeout == NONE)
	l_hdlRFID_DetectTimeout = sTimerCreate (RFID_DetectTimeout);
#endif
}


/***************************************************************************//**
 *
 * @brief	Check if RFID reader is active
 *
 * This routine lets you determine if the RFID reader is in use, i.e. if it
 * has been configured.  It does <b>not</b> tell if the RFID reader is enabled,
 * use IsRFID_Enabled() for this purpose.
 *
 * @see IsRFID_Enabled()
 *
 ******************************************************************************/
bool	IsRFID_Active (void)
{
    return l_flgRFID_Activate;
}


/***************************************************************************//**
 *
 * @brief	Enable RFID reader
 *
 * This routine enables the RFID reader, i.e. it notifies the RFID software
 * module to power up and initialize the reader and the related hardware.
 * It is usually called by PowerControl().
 *
 * @see RFID_Disable(), RFID_TimedDisable(), RFID_Check()
 *
 ******************************************************************************/
void RFID_Enable (void)
{
#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /* set flag to notify there is an object present */
    l_flgObjectPresent = true;

    if (l_hdlRFID_Off != NONE)
	sTimerCancel (l_hdlRFID_Off);	// inhibit power-off of RFID reader
#endif

    /* initiate power-on of the RFID reader */
    l_flgRFID_On = true;

    /* re-trigger "new run" flag */
    l_flgNewRun = true;
    DBG_PUTS(" DBG RFID_Enable: setting l_flgNewRun=1\n");

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /* (re-)start timer for RFID timeout detection */
    DBG_PUTS(" DBG RFID_Enable: starting Detect Timeout\n");
    if (l_hdlRFID_DetectTimeout != NONE)
	sTimerStart (l_hdlRFID_DetectTimeout, g_RFID_DetectTimeout);
#endif
}


/***************************************************************************//**
 *
 * @brief	Disable RFID reader
 *
 * This routine immediately disables the RFID reader.
 *
 * @see RFID_Enable(), RFID_TimedDisable(), RFID_Check()
 *
 ******************************************************************************/
void RFID_Disable (void)
{
#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /* no object present, clear flag */
    l_flgObjectPresent = false;

    /* be sure to cancel timeout timer */
    if (l_hdlRFID_DetectTimeout != NONE)
	sTimerCancel (l_hdlRFID_DetectTimeout);
#endif

    if (l_flgRFID_On)
    {
	l_flgRFID_On = false;		// mark RFID reader to be powered off

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
	if (l_hdlRFID_Off != NONE)
	    sTimerCancel (l_hdlRFID_Off);	// cancel timer
#endif

	/*
	 * If Absent Detection is disabled, the RFID reader is powered off
	 * immediately.  With Absent Detection enabled, the RFID reader is
	 * powered off only if no transponder is present, i.e. <g_Transponder>
	 * is empty.
	 */
	if (g_RFID_AbsentDetectTimeout == 0  ||  g_Transponder[0] == EOS)
	{
	    if (l_hdlRFID_AbsentDetect != NONE)
		sTimerCancel(l_hdlRFID_AbsentDetect);

	    /* RFID reader should be powered OFF immediately */
	    if (l_flgRFID_IsOn)
	    {
		RFID_PowerOff();
		l_flgRFID_IsOn = false;
	    }
	}
#ifdef LOGGING
	else if (g_Transponder[0] != EOS)
	{
	    /* Generate Log Message */
	    Log ("RFID power-off deferred - Bird still present");
	}
#endif
    }
}


/***************************************************************************//**
 *
 * @brief	Check if RFID reader is enabled
 *
 * This routine returns the current power state of the RFID reader.
 *
 * @see RFID_Enable(), RFID_Disable(), RFID_TimedDisable(), RFID_Check()
 *
 ******************************************************************************/
bool	IsRFID_Enabled (void)
{
    return l_flgRFID_On;
}


#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
/***************************************************************************//**
 *
 * @brief	Disable RFID reader after a while
 *
 * This routine disables the RFID reader, i.e. it notifies the RFID software
 * module to power down the reader after a delay of @ref g_RFID_PwrOffTimeout
 * seconds.
 *
 * @see RFID_Enable(), RFID_Disable(), RFID_Check()
 *
 ******************************************************************************/
void RFID_TimedDisable (void)
{
    /* no object present, clear flag */
    l_flgObjectPresent = false;

    /* be sure to cancel timeout timer */
    if (l_hdlRFID_DetectTimeout != NONE)
	sTimerCancel (l_hdlRFID_DetectTimeout);

    /* (re-)start timer to switch the RFID reader OFF after time */
    if (l_hdlRFID_Off != NONE)
	sTimerStart (l_hdlRFID_Off, g_RFID_PwrOffTimeout);
}
#endif	// RFID_TRIGGERED_BY_LIGHT_BARRIER


/***************************************************************************//**
 *
 * @brief	Power RFID reader On
 *
 * This routine powers the RFID reader on and initializes the related hardware.
 *
 ******************************************************************************/
void RFID_PowerOn (void)
{
    if (l_flgRFID_Activate)
    {
#ifdef LOGGING
	/* Generate Log Message */
	Log ("RFID is powered ON");
#endif

	/* Module RFID requires EM1, set bit in bit mask */
	Bit(g_EM1_ModuleMask, EM1_MOD_RFID) = 1;

	/* Prepare UART to receive Transponder ID */
	uartSetup();

	/* Set Power Enable Pin for the RFID receiver to ON */
	PowerOutput (l_pRFID_Cfg.RFID_PwrOut, PWR_ON);

	/* Reset index */
	l_State = 0;
    }
}


/***************************************************************************//**
 *
 * @brief	Power RFID reader Off
 *
 * This routine powers all RFID readers immediately off.
 *
 ******************************************************************************/
void RFID_PowerOff (void)
{
    /* Set Power Enable Pin for the RFID receiver to OFF */
    PowerOutput (l_pRFID_Cfg.RFID_PwrOut, PWR_OFF);

    /* Disable clock for USART module */
    CMU_ClockEnable(l_USART_Parms.cmuClock_UART, false);

    /* Disable Rx pin */
    GPIO_PinModeSet(l_USART_Parms.UART_Rx_Port,
		    l_USART_Parms.UART_Rx_Pin, gpioModeDisabled, 0);

    /* Reset indexes */
    l_State = 0;

    /* Module RFID is no longer active, clear bit in bit mask */
    Bit(g_EM1_ModuleMask, EM1_MOD_RFID) = 0;

#ifdef LOGGING
    /* Generate Log Message */
    Log ("RFID is powered off");
#endif
}


/***************************************************************************//**
 *
 * @brief	RFID Check
 *
 * This function checks if the RFID reader needs to be powered on or off,
 * and if a transponder number has been set.
 *
 * @note
 * 	This function may only be called from standard program, usually the loop
 * 	in file main.c - it must not be called from interrupt routines!
 *
 ******************************************************************************/
void	RFID_Check (void)
{
    if (l_flgRFID_On)
    {
	/* RFID reader should be powered ON */
	if (! l_flgRFID_IsOn)
	{
	    RFID_PowerOn();
	    l_flgRFID_IsOn = true;
	}
    }
    else
    {
	/* RFID reader should be powered OFF */
	if (l_flgRFID_IsOn)
	{
	    /*
	     * If Absent Detection is disabled, the RFID reader is powered off
	     * immediately.  With Absent Detection enabled, the RFID reader is
	     * powered off only if no transponder is present, i.e. <g_Transponder>
	     * is empty.
	     */
	    if (g_RFID_AbsentDetectTimeout == 0  ||  g_Transponder[0] == EOS)
	    {
		if (l_hdlRFID_AbsentDetect != NONE)
		    sTimerCancel(l_hdlRFID_AbsentDetect);

		RFID_PowerOff();
		l_flgRFID_IsOn = false;
	    }
	}
    }

    if (l_flgNewID)
    {
	l_flgNewID = false;

	/* New transponder ID has been set - inform control module */
	ControlUpdateID(g_Transponder);

	/* Also update the LC-Display */
	DisplayUpdate (UPD_TRANSPONDER);
    }
}


/***************************************************************************//**
 *
 * @brief	RFID Power Fail Handler
 *
 * This function will be called in case of power-fail to bring the RFID
 * hardware into a quiescent, power-saving state.
 *
 ******************************************************************************/
void	RFID_PowerFailHandler (void)
{
#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
    /* Cancel timers */
    if (l_hdlRFID_Off != NONE)
	sTimerCancel (l_hdlRFID_Off);

    if (l_hdlRFID_DetectTimeout != NONE)
	sTimerCancel (l_hdlRFID_DetectTimeout);
#endif

    /* Switch RFID reader off */
    l_flgRFID_On = false;

    if (l_flgRFID_IsOn)
    {
	RFID_PowerOff();
	l_flgRFID_IsOn = false;
    }
}


/***************************************************************************//**
 *
 * @brief	Transponder Absent Detection
 *
 * This routine is called from the RTC interrupt handler, after the specified
 * amount of time has elapsed, to notify the latest transponder ID as absent
 * now.
 *
 ******************************************************************************/
static void TransponderAbsent(TIM_HDL hdl)
{
    (void) hdl;		// suppress compiler warning "unused parameter"

#ifdef LOGGING
    Log ("Transponder: %s ABSENT", g_Transponder);
#endif

    /* clear Transponder Number */
    g_Transponder[0] = EOS;

#if RFID_DISPLAY_UPDATE_WHEN_ABSENT
    /* Also update the LC-Display */
    DisplayUpdate (UPD_TRANSPONDER);
#endif
}

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
/***************************************************************************//**
 *
 * @brief	Switch RFID Reader Off
 *
 * This routine is called from the RTC interrupt handler, after the specified
 * amount of time has elapsed, to trigger the power-off of the RFID reader.
 *
 ******************************************************************************/
static void SwitchRFID_Off(TIM_HDL hdl)
{
    (void) hdl;		// suppress compiler warning "unused parameter"

    l_flgRFID_On = false;

    g_flgIRQ = true;	// keep on running
}


/***************************************************************************//**
 *
 * @brief	RFID Detect Timeout occurred
 *
 * This routine is called from the RTC interrupt handler, after the specified
 * RFID timeout has elapsed.  This is the duration, the system waits for the
 * detection of a transponder ID.  If no ID could be received during this time,
 * it is set to "UNKNOWN".
 *
 ******************************************************************************/
static void RFID_DetectTimeout(TIM_HDL hdl)
{
    (void) hdl;		// suppress compiler warning "unused parameter"

    if (l_flgObjectPresent)
    {
	DBG_PUTS(" DBG RFID_DetectTimeout: Detect Timeout over, set UNKNOWN\n");

	strcpy (g_Transponder, "UNKNOWN");

#if defined(LOGGING)  &&  ! defined (MOD_CONTROL_EXISTS)
	/* Generate Log Message if there is no external module to handle this */
	Log ("Transponder: %s", g_Transponder);
#endif
	/* Set flag to notify new transponder ID */
	l_flgNewID = true;
    }
}
#endif	// RFID_TRIGGERED_BY_LIGHT_BARRIER


/***************************************************************************//**
 *
 * @brief	Decode RFID
 *
 * This routine is called from the UART interrupt handler, whenever a new
 * byte has been received.  It contains a state machine to extract a valid
 * transponder ID from the data stream, store it into the global variable
 * @ref g_Transponder, and initiate a display update.  The transponder number
 * is additionally logged, if logging is enabled.
 *
 * The following RFID reader types are supported:
 * - @ref RFID_TYPE_SR : Short Range reader (14 byte frame)
 * - @ref RFID_TYPE_LR : Long Range reader (11 byte frame)
 *
 * @note
 * The RFID reader permanently sends the ID as long as the transponder resides
 * in its range.  To prevent a huge amount of log messages, the received data
 * is compared with the previous ID.  It will only be logged if it differs,
 * or the flag @ref l_flgNewRun is set, which indicates a transponder detection
 * after a period of "absence".
 *
 ******************************************************************************/
static void RFID_Decode(uint32_t byte)
{
const  uint8_t	 v[5] = { 0x0E, 0x00, 0x11, 0x00, 0x05};
const  char	 HexChar[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
 				'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
static uint8_t	 xorsum;  // checksum variables
static uint8_t	 w[14];   // buffer for storing received bytes
static uint16_t	 crc;     // checksum variables
uint16_t	 val;
bool	 flgRecvdID = false;
char	 newTransponder[50]; // also used to store data in case of error message
int	 i, pos;


    /* count communication errors for debugging purposes */
    if (byte & USART_RXDATAX_FERR)
	g_FERR_Cnt++;

    if (byte & USART_RXDATAX_PERR)
	g_PERR_Cnt++;

    /* store current byte into receive buffer */
    byte &= 0xFF;		// only bit 7~0 contains the data
    DBG_PUTC('[');DBG_PUTC(HexChar[(byte >> 4) & 0xF]);
    DBG_PUTC(HexChar[byte & 0xF]);DBG_PUTC(']');
    w[l_State] = (uint8_t)byte;

    /* handle data according to the respective RFID reader type */
    switch (l_pRFID_Cfg.RFID_Type)
    {
	case RFID_TYPE_SR:	// Short Range reader (14 byte frame, 133kHz)
	    /*
	     * NOTE: Most of the code has been taken from file "RFID_tag.c"
	     */
	    switch (l_State)	// the state machine!
	    {
	    case 0:
		xorsum = 0;
		/* no break */
	    case 1:
	    case 2:
	    case 3:
	    case 4:
		// Verify prefix
		if (byte != v[l_State])
		{
		    l_State = 0;	// restart state machine
		    break;		// break!
		}
		/* no break */
	    case 5:
	    case 6:
	    case 7:
	    case 8:
	    case 9:
	    case 10:
	    case 11:
	    case 12:
		xorsum ^= byte;		// build checksum
		l_State++;		// go on
		break;			// break!

	    case 13:
		if (w[13] != xorsum)	// Checksumme vergleichen!
		{
		    pos = 0;
		    for (i=0; i <= 13; i++)
		    {
			newTransponder[pos++] = ' ';
			newTransponder[pos++] = HexChar[(w[i] >> 4) & 0x0F];
			newTransponder[pos++] = HexChar[(w[i]) & 0x0F];
		    }
		    newTransponder[pos] = '\0';
		    LogError("RFID_Decode(): recv.XOR=0x%02X, calc.XOR=0x%02X,"
			     " data is%s", w[13], xorsum, newTransponder);
		    l_State = 0;	// restart state machine
		    break;
		}

		flgRecvdID = true;	// ID has been received - set flag
		break;

	    default:
		l_State = 0;	// restart state machine
		break;
	    }
	    break;

	case RFID_TYPE_LR:	// Long Range reader (11 byte frame, 133kHz)
	    switch (l_State)	// the state machine!
	    {
	    case 0:	// expect prefix 0x54 ('T')
		if (byte != 0x54)
		{		// (there may be leading zeros)
		    l_State = 0; // restart state machine
		    break;	// break!
		}
		/* no break */
	    case 1:
		crc = 0x0000;	// init w/ 0
		/* no break */
	    case 2:
	    case 3:
	    case 4:
	    case 5:
	    case 6:
	    case 7:
	    case 8:
		/*
		 * calculate CRC-CCITT as used by KERMIT (the protocol,
		 * not the frog ;-)  polynomial: x^16+x^12+x^5+1
		 * http://www.columbia.edu/kermit/ftp/e/kproto.doc
		 */
		val = crc;
		val = (val >> 4) ^ (((val ^ (byte >> 0)) & 0x0F) * 4225);
		val = (val >> 4) ^ (((val ^ (byte >> 4)) & 0x0F) * 4225);
		crc = val;
		/* no break */
	    case 9:
		l_State++;	// increase position in buffer
		break;			// break!

	    case 10:	// received complete frame
		val = (w[10] << 8) | w[9];
		if (val != crc)		// Checksumme vergleichen!
		{
		    pos = 0;
		    for (i=0; i <= 10; i++)
		    {
			newTransponder[pos++] = ' ';
			newTransponder[pos++] = HexChar[(w[i] >> 4) & 0x0F];
			newTransponder[pos++] = HexChar[(w[i]) & 0x0F];
		    }
		    newTransponder[pos] = '\0';
		    LogError("RFID_Decode(): recv.CRC=0x%04X, calc.CRC=0x%04X,"
			     " data is%s", val, crc, newTransponder);
		    l_State = 0;	// restart state machine
		    break;
		}

		flgRecvdID = true;	// ID has been received - set flag
		break;

	    default:
		l_State = 0;	// restart state machine
		break;
	    }
	    break;

	default:		// unknown RFID reader type
	    l_State = 0;	// restart state machine
	    break;
    }

    /* see if a transponder ID has been received */
    if (flgRecvdID)
    {
	l_State = 0;		// restart state machine

	for (i=0; i < 8; i++)	// copy w and convert to ASCII HEX
	{
	    newTransponder[2*i]	  = HexChar[(w[8-i]>>4) & 0x0F];
	    newTransponder[2*i+1] = HexChar[(w[8-i]) & 0x0F];
	}
	newTransponder[16] = '\0';

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
	/* (re-)start timer for RFID timeout detection */
	if (l_flgObjectPresent  &&  l_hdlRFID_DetectTimeout != NONE)
	    sTimerStart (l_hdlRFID_DetectTimeout, g_RFID_DetectTimeout);
#endif

	/* see if a new run - or Transponder Number has changed */
	if (l_flgNewRun  ||  strcmp (newTransponder, g_Transponder))
	{
	    l_flgNewRun = false;	// clear flag

	    /* store new Transponder Number */
	    strcpy (g_Transponder, newTransponder);

#if defined(LOGGING)  &&  ! defined (MOD_CONTROL_EXISTS)
	    /* Generate Log Message */
	    Log ("Transponder: %s", g_Transponder);
#endif
	    /* Set flag to notify new transponder ID */
	    l_flgNewID = true;
	}

	/* ALWAYS (re-)trigger timer if an ID has been received */
	if (g_RFID_AbsentDetectTimeout > 0)
	    sTimerStart (l_hdlRFID_AbsentDetect,
			 g_RFID_AbsentDetectTimeout);
    }
}


/*============================================================================*/
/*=============================== UART Routines ==============================*/
/*============================================================================*/

/* Setup UART in async mode for RS232*/
static USART_InitAsync_TypeDef uartInit = USART_INITASYNC_DEFAULT;


/******************************************************************************
* @brief  uartSetup function
*
******************************************************************************/
static void uartSetup(void)
{
int	type = l_pRFID_Cfg.RFID_Type;

  /* Enable clock for USART module */
  CMU_ClockEnable(l_USART_Parms.cmuClock_UART, true);

  /* Configure GPIO Rx pin */
  GPIO_PinModeSet(l_USART_Parms.UART_Rx_Port,
		  l_USART_Parms.UART_Rx_Pin, gpioModeInput, 0);

  /* Prepare struct for initializing UART in asynchronous mode */
  uartInit.enable       = usartDisable;   // Don't enable UART upon initialization
  uartInit.refFreq      = 0;              // Set to 0 to use reference frequency
  uartInit.baudrate     = l_RFID_Type_Parms[type].Baudrate;
  uartInit.oversampling = usartOVS16;     // Oversampling. Range is 4x, 6x, 8x or 16x
  uartInit.databits     = l_RFID_Type_Parms[type].DataBits;
  uartInit.parity       = l_RFID_Type_Parms[type].Parity;
  uartInit.stopbits     = l_RFID_Type_Parms[type].StopBits;
#if defined( USART_INPUT_RXPRS ) && defined( USART_CTRL_MVDIS )
  uartInit.mvdis        = false;          // Disable majority voting
  uartInit.prsRxEnable  = false;          // Enable USART Rx via Peripheral Reflex System
  uartInit.prsRxCh      = usartPrsRxCh0;  // Select PRS channel if enabled
#endif

  /* Initialize USART with uartInit struct */
  USART_InitAsync(l_USART_Parms.UART, &uartInit);

  /* Prepare UART Rx interrupts */
  USART_IntClear(l_USART_Parms.UART, _USART_IF_MASK);
  USART_IntEnable(l_USART_Parms.UART, USART_IF_RXDATAV);
  NVIC_SetPriority(l_USART_Parms.UART_Rx_IRQn, INT_PRIO_UART);
  NVIC_ClearPendingIRQ(l_USART_Parms.UART_Rx_IRQn);
  NVIC_EnableIRQ(l_USART_Parms.UART_Rx_IRQn);

  /* Enable I/O pins at UART location #2 */
  l_USART_Parms.UART->ROUTE = USART_ROUTE_RXPEN | l_USART_Parms.UART_Route;

  /* Enable UART receiver only */
  USART_Enable(l_USART_Parms.UART, usartEnableRx);
}


/**************************************************************************//**
 *
 * @brief UART 1 RX IRQ Handler
 *
 * This interrupt service routine is called whenever a byte has been received
 * from the RFID reader associated with UART 1.  It calls RFID_Decode() to
 * extract a valid ID from the data stream.
 *
 * NOTE:
 * Since both UARTs use the same interrupt priority, no interference is
 * possible between the two UARTs.
 *
 *****************************************************************************/
void USART1_RX_IRQHandler(void)
{
    DEBUG_TRACE(0x07);

    /* Check for RX data valid interrupt */
    if (USART1->STATUS & USART_STATUS_RXDATAV)
    {
	/* Decode data */
	RFID_Decode (USART1->RXDATA);

	/* Clear RXDATAV interrupt */
	USART_IntClear(USART1, USART_IF_RXDATAV);
    }

    DEBUG_TRACE(0x87);
}
