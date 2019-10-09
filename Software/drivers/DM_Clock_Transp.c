/***************************************************************************//**
 * @file
 * @brief	Display Module: Clock and Transponder
 * @author	Ralf Gerhauser
 * @version	2018-03-08
 *
 * This file provides a display module that shows the current time and
 * transponder number or a temporary message.  A further menu displays the
 * Firmware Version and allows you to clear the field for transponder number
 * by asserting the <i>Down-Key</i>.  During the second menu the RFID Reader
 * is activated for test purposes.
 *
 * Menu Structure:
 @verbatim
----[180321 17:10:22 ]----[Firmware Version   ]
    [Transponder #   ]    [CLEAR Transponder #]
 @endverbatim
 *
 ****************************************************************************//*
Revision History:
2018-03-08,rage	Initial version, based on project "AlarmClock".
*/

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <string.h>
#include "em_device.h"
#include "em_assert.h"
#include "clock.h"		// include g_rtcStartTime
#include "AlarmClock.h"
#include "DisplayMenu.h"
#include "RFID.h"
#include "DM_Clock_Transp.h"
#include "LCD_DOGM162.h"	// defines LCD_ARROW_DOWN

/*=========================== Forward Declarations ===========================*/

static void	DispTimeTransp (UPD_ID updId);
static KEYCODE	MenuClearTransp (KEYCODE keycode, uint32_t arg);
static void	DispVersionClearTransp (UPD_ID updId);

static DISP_MOD * const l_DM_List[];

/*================================ Global Data ===============================*/

extern PRJ_INFO const  prj;		// Project Information

/*! Module to display the current time and the latest transponder ID. */
DISP_MOD DM_TimeTransp =
{
    .MenuFct	= MenuDistributor,
    .Arg	= 0,
    .DispFct	= DispTimeTransp,
    .pNextMenu	= l_DM_List		// local DM list
};

/*================================ Local Data ================================*/

static bool	l_flgOrgRFID_Active;	// original state: RFID is active?

static DISP_MOD DM_VersionClearTransp =
{
    .MenuFct	= MenuClearTransp,
    .Arg	= 0,
    .DispFct	= DispVersionClearTransp,
    .pNextMenu	= NULL
};

static DISP_MOD * const l_DM_List[] =
{
    &DM_VersionClearTransp,
    NULL
};


/***************************************************************************//**
 *
 * @brief	Display Time
 *
 * This routine is used to display the current system time and the latest
 * transponder ID.
 *
 ******************************************************************************/
static void	DispTimeTransp (UPD_ID updId)
{
    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	case UPD_SYSCLOCK:	// System Clock (time, date) was updated
	    DispPrintf (1, "%02d%02d%02d %02d:%02d:%02d",
			g_CurrDateTime.tm_year,
			g_CurrDateTime.tm_mon + 1,
			g_CurrDateTime.tm_mday,
			g_CurrDateTime.tm_hour,
			g_CurrDateTime.tm_min,
			g_CurrDateTime.tm_sec);
	    if (updId != UPD_ALL)
		break;
	    /* no break */

	case UPD_TRANSPONDER:	// current transponder number
	    DispPrintf (2, g_Transponder);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Menu Handler for clearing the Transponder number
 *
 * This menu handler clears the transponder number when the <i>Down-Key</i>
 * is asserted.
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
 *	Normally KEYCODE_MENU_UPDATE if LCD should be updated, or KEYCODE_NONE
 *	if nothing to do.  If a module cannot handle a key, the key code is
 *	returned to the menu key handler.
 *
 * @warning
 *	This routine is executed in interrupt context!
 *
 ******************************************************************************/
static KEYCODE MenuClearTransp (KEYCODE keycode, uint32_t arg)
{
static bool	flgOrgRFID_Enabled;	// original state: RFID is enabled?

    (void) arg;			// suppress compiler warning "unused parameter"

    /*
     * Handle key code - most of them need not to be handled, so the code is
     * simply returned to the MenuKeyHandler()
     */
    switch (keycode)
    {
	case KEYCODE_MENU_ENTER:	/*---------- Enter this menu ---------*/
	    l_flgOrgRFID_Active = IsRFID_Active();
	    if (l_flgOrgRFID_Active)
	    {
		flgOrgRFID_Enabled = IsRFID_Enabled();
		if (! flgOrgRFID_Enabled)
		{
		    /* enable RFID reader for test purposes */
		    RFID_Enable();
		}
	    }
	    break;

	case KEYCODE_MENU_EXIT:		/*---------- Leave this menu ---------*/
	    if (l_flgOrgRFID_Active)
	    {
		/* let the RFID reader return to its original state */
		if (flgOrgRFID_Enabled != IsRFID_Enabled())
		{
		    if (flgOrgRFID_Enabled)
			RFID_Enable();
		    else
			RFID_Disable();
		}
	    }
	    break;

	case KEYCODE_DOWN_ASSERT:	/*------- Clear Transponder ID -------*/
	    g_Transponder[0] = EOS;
	    DisplayUpdate (UPD_TRANSPONDER);
	    return KEYCODE_NONE;	// nothing to do

	default:			/*------ Forward all other keys ------*/
	    break;
    }

    return keycode;	// let the calling routine handle the key
}

/***************************************************************************//**
 *
 * @brief	Display Version and clear Transponder ID
 *
 * This routine shows the firmware version in the upper line of the LC-Display.
 * Additionally it allows to clear the transponder ID by asserting the
 * <i>Down-Key</i>.  This is useful when testing the RFID receiver with the
 * the same transponder to make the detection of the ID visible.
 *
 ******************************************************************************/
static void	DispVersionClearTransp (UPD_ID updId)
{
    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    DispPrintf (1, "V%s %s", prj.Version, prj.Date);
	    if (l_flgOrgRFID_Active)
		DispPrintf (2, LCD_ARROW_DOWN ":CLEAR Transp.#");
	    else
		DispPrintf (2, "RFID: NO CONFIG");
	    break;

	case UPD_TRANSPONDER:	// current transponder number (or cleared)
	    if (l_flgOrgRFID_Active)
		DispPrintf (2, g_Transponder);
	    break;

	default:
	    break;
    }
}
