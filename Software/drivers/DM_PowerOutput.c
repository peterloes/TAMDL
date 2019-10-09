/***************************************************************************//**
 * @file
 * @brief	Display Module: Power Outputs
 * @author	Ralf Gerhauser
 * @version	2018-03-14
 *
 * This file provides display modules for the following tasks:
 * - Manually enabling the Power Outputs and measuring the actual voltage
 *   and current.
 * - Calibrating the measurement of @ref Power_Outputs UA1 and UA2, based on
 *   the specified voltage [mV] and current [mA] reference values via the
 *   configuration variables @ref CALIBRATE_mV and @ref CALIBRATE_mA in
 *   file <a href="../../CONFIG.TXT"><i>CONFIG.TXT</i></a>.
 *
 * Menu Structure:
 @verbatim
+---[Power Output   ]---+--[UA1  5.2V 2305mA]
|   [[ua1][UA2][bat]]   |  [SET: enable Pwr ]
|                       |
|                       +--[UA2  9.0V 1738mA]
|                       |  [SET: disable Pwr]
|                       |
|                       +--[BATT Output     ]
|                          [SET: enable Pwr ]
|
+---[Calibration of ]---+--[SET: Calibr. UA1]
|   [UA-Measurement ]   |  [@  9532mV 1230mA]
|                       |
|                       +--[UA2 CALIBRATION ]
|                          [IS NOT POSSIBLE ]
 @endverbatim
 *
 ****************************************************************************//*
Revision History:
2018-03-14,rage	Initial version.
*/

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <string.h>
#include "em_device.h"
#include "em_assert.h"
#include "clock.h"		// include g_rtcStartTime
#include "AlarmClock.h"
#include "DisplayMenu.h"
#include "Control.h"
#include "DM_PowerOutput.h"

/*=========================== Forward Declarations ===========================*/

static void	DispPowerStatus (UPD_ID updId);
static KEYCODE	MenuPowerOutput (KEYCODE keycode, uint32_t arg);
static void	DispPowerOutput (UPD_ID updId);

static KEYCODE	MenuCalibration (KEYCODE keycode, uint32_t arg);
static void	DispCalibration (UPD_ID updId);
static KEYCODE	MenuCalibrateOutput (KEYCODE keycode, uint32_t arg);
static void	DispCalibrateOutput (UPD_ID updId);

static DISP_MOD * const l_DM_StatusList[];
static DISP_MOD * const l_DM_CalibrList[];

/*================================ Global Data ===============================*/

/*! Module to display Status of the Power Outputs. */
DISP_MOD DM_PowerStatus =
{
    .MenuFct	= MenuDistributor,	// generic menu distributor
    .Arg	= 0,
    .DispFct	= DispPowerStatus,
    .pNextMenu	= l_DM_StatusList	// local DM list
};

/*! Module to display Calibration Menu. */
DISP_MOD DM_Calibration =
{
    .MenuFct	= MenuCalibration,	// handler for calibration menu
    .Arg	= 0,
    .DispFct	= DispCalibration,
    .pNextMenu	= l_DM_CalibrList	// local DM list
};

    /*!@brief Calibration values for Power Outputs UA1, UA2 in [mV]. */
uint32_t		 g_UA_Calib_mV[2];

    /*!@brief Calibration values for Power Outputs UA1, UA2 in [mA]. */
uint32_t		 g_UA_Calib_mA[2];

/*================================ Local Data ================================*/

    /*!@brief Actual selection of power output. */
static PWR_OUT	l_PwrOut;

    /*!@brief Flags to remember which power channels have been calibrated. */
static uint8_t	l_flgCalibration;

    /* Status of dedicated Power Outputs */
static DISP_MOD DM_PowerOutputUA1 =
{
    .MenuFct	= MenuPowerOutput,
    .Arg	= PWR_OUT_UA1,
    .DispFct	= DispPowerOutput,
    .pNextMenu	= NULL
};

static DISP_MOD DM_PowerOutputUA2 =
{
    .MenuFct	= MenuPowerOutput,
    .Arg	= PWR_OUT_UA2,
    .DispFct	= DispPowerOutput,
    .pNextMenu	= NULL
};

static DISP_MOD DM_PowerOutputBATT =
{
    .MenuFct	= MenuPowerOutput,
    .Arg	= PWR_OUT_BATT,
    .DispFct	= DispPowerOutput,
    .pNextMenu	= NULL
};

static DISP_MOD * const l_DM_StatusList[] =
{
    &DM_PowerOutputUA1,
    &DM_PowerOutputUA2,
    &DM_PowerOutputBATT,
    NULL
};

    /* Calibration of dedicated Power Outputs */
static DISP_MOD DM_CalibrateOutputUA1 =
{
    .MenuFct	= MenuCalibrateOutput,
    .Arg	= PWR_OUT_UA1,
    .DispFct	= DispCalibrateOutput,
    .pNextMenu	= NULL
};

static DISP_MOD DM_CalibrateOutputUA2 =
{
    .MenuFct	= MenuCalibrateOutput,
    .Arg	= PWR_OUT_UA2,
    .DispFct	= DispCalibrateOutput,
    .pNextMenu	= NULL
};

static DISP_MOD * const l_DM_CalibrList[] =
{
    &DM_CalibrateOutputUA1,
    &DM_CalibrateOutputUA2,
    NULL
};


/***************************************************************************//**
 *
 * @brief	Display Power Status
 *
 * This routine displays the current power status, i.e. what outputs are
 * currently enabled or disabled.  Upper case letters means enabled, while
 * lower case letters means disabled.
 *
 ******************************************************************************/
static void	DispPowerStatus (UPD_ID updId)
{
    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    DispPrintf (1, "Power Output");
	    if (updId != UPD_ALL)
		break;
	    /* no break */

	case UPD_POWERSTATUS:	// Power output has been changed
	    DispPrintf (2, "[%s][%s][%s]",
			IsPowerOutputOn(PWR_OUT_UA1)  ? "UA1":"ua1",
			IsPowerOutputOn(PWR_OUT_UA2)  ? "UA2":"ua2",
			IsPowerOutputOn(PWR_OUT_BATT) ? "BATT":"batt");
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Menu Handler to control Power Outputs
 *
 * Toggle the selected power output on or off via the <i>Set-Key</i>.
 *
 * @param[in] keycode
 *	Translated key code, sent by the menu key handler to this module.
 *	If a module cannot handle a key, it must be returned to the key
 *	handler.
 *
 * @param[in] arg
 *	This argument selects the power output to switch.
 *
 * @return
 *	If a module cannot handle a key, the key code is returned to the menu
 *	key handler.
 *
 * @warning
 *	This routine is executed in interrupt context!
 *
 ******************************************************************************/
static KEYCODE MenuPowerOutput (KEYCODE keycode, uint32_t arg)
{
    l_PwrOut = (PWR_OUT) arg;		// save power channel

    /*
     * Handle key code - most of them need not to be handled, so the code is
     * simply returned to the MenuKeyHandler().
     */
    switch (keycode)
    {
	case KEYCODE_SET_RELEASE:	/*------ En/Disable Power Output -----*/
	    /* SET-Key toggles Power Output ON/OFF */
	    PowerOutput (l_PwrOut, IsPowerOutputOn(l_PwrOut) ? PWR_OFF:PWR_ON);
	    return KEYCODE_NONE;	// nothing to do

	default:			/*------ Forward all other keys ------*/
	    break;
    }

    return keycode;	// let the calling routine handle the key
}

/***************************************************************************//**
 *
 * @brief	Display actual Status of a Power Output
 *
 * This routine allows to switch a Power Output on or off by asserting the
 * <i>Set-Key</i>.  While power is on, the actual voltage and current is
 * displayed.
 *
 ******************************************************************************/
static void	DispPowerOutput (UPD_ID updId)
{
uint32_t value_mV;
uint32_t value_mA;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	case UPD_SYSCLOCK:	// update value with SYSCLOCK (every second)
	    switch (l_PwrOut)
	    {
		case PWR_OUT_UA1:
		case PWR_OUT_UA2:
		    value_mV = PowerVoltage(l_PwrOut) + 50;	// round
		    value_mA = PowerCurrent(l_PwrOut);
		    DispPrintf (1, "UA%d %2ld.%ldV %4ldmA",
				l_PwrOut-PWR_OUT_UA1+1,
				(value_mV / 1000), (value_mV % 1000) / 100,
				value_mA);
		    break;

		case PWR_OUT_BATT:
		    DispPrintf (1, "BATT Output");
		    break;

		default:	// unknown power channel (error)
		    DispPrintf (1, "ERR: UNKNOWN OUT");
		    break;
	    }

	    if (updId != UPD_ALL)
		break;
	    /* no break */

	case UPD_POWERSTATUS:
	    DispPrintf (2, "SET: %sable Pwr",
			IsPowerOutputOn(l_PwrOut)  ? "dis":"en");
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Menu Handler for Calibration
 *
 * This menu handler leads the user to the calibration menu for UA1 and UA2
 * measurement.  If one or more calibration procedures have been done, this
 * handler saves the calibration data to flash, when returning from these
 * menus.
 *
 * @param[in] keycode
 *	Translated key code, sent by the menu key handler to this module.
 *	If a module cannot handle a key, it must be returned to the key
 *	handler.
 *
 * @param[in] arg
 *	Optional argument, not used here.
 *
 * @return
 *	If a module cannot handle a key, the key code is returned to the menu
 *	key handler.
 *
 * @warning
 *	This routine is executed in interrupt context!
 *
 ******************************************************************************/
static KEYCODE MenuCalibration (KEYCODE keycode, uint32_t arg)
{
    (void) arg;			// suppress compiler warning "unused parameter"

    /*
     * Handle key code - most of them need not to be handled, so the code is
     * simply returned to the MenuKeyHandler().
     */
    switch (keycode)
    {
	case KEYCODE_MENU_ENTER:	/*---------- Enter this menu ---------*/
	    if (l_flgCalibration !=  0)	// calibration was performed,
		WriteCalibrationData();	// save data to flash
	    break;

	case KEYCODE_MENU_EXIT:		/*---------- Leave this menu ---------*/
	    l_flgCalibration = 0;	// clear flags
	    break;

	default:			/*------ Forward all other keys ------*/
	    break;
    }

    return keycode;	// let the calling routine handle the key
}

/***************************************************************************//**
 *
 * @brief	Display Menu Entry for Calibration
 *
 * This routine displays the message "Calibration of UA-Measurement" on the LCD.
 *
 ******************************************************************************/
static void	DispCalibration (UPD_ID updId)
{
    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    DispPrintf (1, "Calibration of");
	    DispPrintf (2, "UA-Measurement");
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Menu Handler to calibrate UA1 and UA2 measurements
 *
 * This menu handler performs the calibration for the UA1 and UA2 measurements
 * by asserting the <i>Set-Key</i>.  This is only possible, if dedicated
 * calibration values ([mV] and [mA]) have been provided via
 *
 * @param[in] keycode
 *	Translated key code, sent by the menu key handler to this module.
 *	If a module cannot handle a key, it must be returned to the key
 *	handler.
 *
 * @param[in] arg
 *	Optional argument, not used here.
 *
 * @return
 *	If a module cannot handle a key, the key code is returned to the menu
 *	key handler.
 *
 * @warning
 *	This routine is executed in interrupt context!
 *
 ******************************************************************************/
static KEYCODE MenuCalibrateOutput (KEYCODE keycode, uint32_t arg)
{
int	 idx = arg - PWR_OUT_UA1;

    l_PwrOut = (PWR_OUT) arg;		// save power channel

    /* Cancel Timer to keep Display switched on */
    DisplayTimerCancel();

    /*
     * Handle key code - most of them need not to be handled, so the code is
     * simply returned to the MenuKeyHandler().
     */
    switch (keycode)
    {
	case KEYCODE_MENU_ENTER:	/*------ Switch Power Output ON ------*/
	    PowerOutput (l_PwrOut, PWR_ON);
	    break;

	case KEYCODE_MENU_EXIT:		/*------ Switch Power Output OFF -----*/
	    PowerOutput (l_PwrOut, PWR_OFF);
	    break;

	case KEYCODE_SET_RELEASE:	/*-------- Trigger Calibration -------*/
	    /* SET-Key triggers Calibration (if possible) */
	    if (g_UA_Calib_mV[idx] == 0)
		break;			// CALIBRATION IS NOT POSSIBLE

	    /* Calibrate voltage and current measurement for this output */
	    CalibrateVoltage(l_PwrOut, g_UA_Calib_mV[idx]);
	    CalibrateCurrent(l_PwrOut, g_UA_Calib_mA[idx]);
	    l_flgCalibration |= (1 << idx);	// mark channel as calibrated
	    return KEYCODE_NONE;	// nothing to do

	default:			/*------ Forward all other keys ------*/
	    break;
    }

    return keycode;	// let the calling routine handle the key
}

/***************************************************************************//**
 *
 * @brief	Display Calibration Messages
 *
 * This routine tells the user if calibration of the selected measurement is
 * currently possible.  If configuration variables with a nominal voltage and
 * current are available, these values are displayed in the second line of the
 * LCD.  After calibration is done, the actual voltage and current are shown
 * every second.
 *
 ******************************************************************************/
static void	DispCalibrateOutput (UPD_ID updId)
{
int	 idx = l_PwrOut - PWR_OUT_UA1;
uint32_t value_mV;
uint32_t value_mA;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	case UPD_CONFIGURATION:	// Configuration variables have been changed
	    if (g_UA_Calib_mV[idx] == 0)
	    {
		DispPrintf (1, "UA%d CALIBRATION", idx+1);
		DispPrintf (2, "IS NOT POSSIBLE");
	    }
	    else
	    {
		DispPrintf (1, "SET: Calibr. UA%d", idx+1);
		DispPrintf (2, "@ %5lumV %4lumA",
			    g_UA_Calib_mV[idx], g_UA_Calib_mA[idx]);
	    }
	    break;

	case UPD_SYSCLOCK:	// update value with SYSCLOCK (every second)
	    if (l_flgCalibration & (1 << idx))
	    {
		/* this channel has been calibrated - show voltage and current */
		value_mV = PowerVoltage(l_PwrOut) + 50;	// round
		value_mA = PowerCurrent(l_PwrOut);
		DispPrintf (1, "UA%d %2ld.%ldV %4ldmA", l_PwrOut-PWR_OUT_UA1+1,
			    (value_mV / 1000), (value_mV % 1000) / 100,
			    value_mA);
	    }
	    break;

	default:
	    break;
    }
}
