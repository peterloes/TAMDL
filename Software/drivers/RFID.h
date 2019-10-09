/***************************************************************************//**
 * @file
 * @brief	Header file of module RFID.c
 * @author	Ralf Gerhauser
 * @version	2018-03-26
 ****************************************************************************//*
Revision History:
2018-03-26,rage - Defined switch RFID_DISPLAY_UPDATE_WHEN_ABSENT.
		- RFID_TRIGGERED_BY_LIGHT_BARRIER lets you select whether the
		  RFID reader is controlled by light-barriers or alarm times.
		- Set default value for DFLT_RFID_ABSENT_DETECT_TIMEOUT to 5s.
		- Added prototypes for IsRFID_Active() and IsRFID_Enabled().
2017-06-20,rage	Defined RFID_TYPE, extended RFID_CONFIG with it.
2017-05-02,rage	RFID_Init: Added RFID_PWR_OUT structure and NUM_RFID_READER.
2016-02-24,rage	Added prototype for RFID_PowerOff().
2014-11-25,rage	Initial version.
*/

#ifndef __INC_RFID_h
#define __INC_RFID_h

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <stdbool.h>
#include "em_device.h"
#include "config.h"		// include project configuration parameters
#include "Control.h"

/*=============================== Definitions ================================*/

    /*!@brief Flag, if display should be cleared when transponder gets absent */
#ifndef RFID_DISPLAY_UPDATE_WHEN_ABSENT
    #define RFID_DISPLAY_UPDATE_WHEN_ABSENT	0
#endif

    /*!@brief Flag, if RFID reader is triggered by a light barrier. */
#ifndef RFID_TRIGGERED_BY_LIGHT_BARRIER
    #define RFID_TRIGGERED_BY_LIGHT_BARRIER	1
#endif

    /*!@brief Timeout in [s] after which a transponder is treated as absent. */
#ifndef DFLT_RFID_ABSENT_DETECT_TIMEOUT
    #define DFLT_RFID_ABSENT_DETECT_TIMEOUT	5
#endif

    /*!@brief Duration in [s] after which the RFID reader is powered-off. */
#ifndef DFLT_RFID_POWER_OFF_TIMEOUT
    #define DFLT_RFID_POWER_OFF_TIMEOUT		30
#endif

    /*!@brief Duration in [s] during the RFID reader tries to read an ID. */
#ifndef DFLT_RFID_DETECT_TIMEOUT
    #define DFLT_RFID_DETECT_TIMEOUT		10
#endif


    /*!@brief RFID types. */
typedef enum
{
    RFID_TYPE_NONE = NONE,	// (-1) for no RFID at all
    RFID_TYPE_SR,		// 0: Short Range RFID reader
    RFID_TYPE_LR,		// 1: Long Range RFID reader
    NUM_RFID_TYPE
} RFID_TYPE;

/*!@brief Structure to specify the type of RFID readers and the power outputs */
typedef struct
{
    RFID_TYPE		RFID_Type;		//!< RFID type selection
    PWR_OUT		RFID_PwrOut;		//!< Power output selection
} RFID_CONFIG;

/*================================ Global Data ===============================*/

#if RFID_TRIGGERED_BY_LIGHT_BARRIER	// only required for light barriers
extern int32_t	 g_RFID_PwrOffTimeout;
extern int32_t	 g_RFID_DetectTimeout;
#endif
extern RFID_TYPE g_RFID_Type;
extern PWR_OUT	 g_RFID_Power;
extern uint32_t	 g_RFID_AbsentDetectTimeout;
extern const char *g_enum_RFID_Type[];
extern char	 g_Transponder[18];

/*================================ Prototypes ================================*/

    /* Initialize the RFID module */
void	RFID_Init (void);

    /* Check if RFID reader is active */
bool	IsRFID_Active (void);

    /* Enable RFID reader */
void	RFID_Enable (void);

    /* Disable RFID reader */
void	RFID_Disable (void);

    /* Check if RFID reader is enabled */
bool	IsRFID_Enabled (void);

    /* Disable RFID reader after a while */
void	RFID_TimedDisable (void);

    /* Check if to power-on/off RFID reader, get transponder number */
void	RFID_Check (void);

    /* Power RFID reader Off */
void	RFID_PowerOff (void);

    /* RFID Power Fail Handler */
void	RFID_PowerFailHandler (void);


#endif /* __INC_RFID_h */
