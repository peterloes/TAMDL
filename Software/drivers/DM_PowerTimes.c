/***************************************************************************//**
 * @file
 * @brief	Display Module: Power Times
 * @author	Ralf Gerhauser
 * @version	2018-10-10
 *
 * This display module allows you to show all ON and OFF times of the @ref
 * Power_Outputs, as configured by the following variables:
 * - @ref UA1_ON_TIME_1
 * - @ref UA2_ON_TIME_1
 * - @ref BATT_ON_TIME_1
 *
 * If a time entry is not used, "--:--" will be displayed.  Entries that do not
 * exist, won't be displayed at all (this only happens, if the number of ON
 * times and OFF times is not the same).
 *
 * Menu Structure:
 @verbatim
----[ON/Off-Times for]---+--[UA1    ON : 2   ]---+--[UA1  Intvl: 300s]
    [Power Outputs   ]   |  [Times  OFF: 3   ]   |  [OnDuration: 120s]
                         |                       |
                         |                       +--[UA1   ON  03:25 ]
                         |                       |  [#1    OFF 06:00 ]
                         |                       |         ...
                         |                       +--[UA1   ON  --:-- ]
                         |                          [#5    OFF 08:30 ]
                         |
                         +--[UA2    ON : 2   ]---+--[UA2 Power Cycle ]
                         |  [Times  OFF: 3   ]   |  [Config-Error    ]
                         |                       |
                         |                       +--[UA2   ON  10:00 ]
                         |                       |  [#1    OFF 12:30 ]
                         |                       |         ...
                         |                       +--[UA2   ON  --:-- ]
                         |                          [#5    OFF --:-- ]
                         |
                         +--[BATT   ON : 2   ]---+--[BATT Power Cycle]
                            [Times  OFF: 3   ]   |  [is disabled     ]
                                                 |
                                                 +--[BATT  ON  --:-- ]
                                                 |  [#1    OFF --:-- ]
                                                 |         ...
                                                 +--[BATT  ON  --:-- ]
                                                    [#5    OFF --:-- ]
 @endverbatim
 *
 ****************************************************************************//*
Revision History:
2018-10-10,rage Added menu entry for Power Cycle Interval and On Duration.
2018-03-21,rage	Initial version.
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
#include "DM_PowerTimes.h"

/*=============================== Definitions ================================*/

/*!@brief Structure to specify ranges of On/Off-Times for each power output. */
typedef struct
{
    const char		*PowerOutName;	//!< Name of the Power Output
    const ALARM_ID	 OnTimeBase;	//!< Base enum for On-Times of this range
    const uint8_t	 OnTimeCount;	//!< Number of On-Time entries
    const ALARM_ID	 OffTimeBase;	//!< Base enum for Off-Times of this range
    const uint8_t	 OffTimeCount;	//!< Number of Off-Time entries
} ON_OFF_TIME_RANGE;

/*=========================== Forward Declarations ===========================*/

static void	DispPowerTimesMain (UPD_ID updId);
static KEYCODE	MenuPowerTimes (KEYCODE keycode, uint32_t arg);
static void	DispPowerTimes (UPD_ID updId);

static DISP_MOD * const l_DM_List[];

/*================================ Global Data ===============================*/

/*! Module to display ON and OFF times for the power outputs. */
DISP_MOD DM_PowerTimes =
{
    .MenuFct	= MenuDistributor,
    .Arg	= 0,
    .DispFct	= DispPowerTimesMain,
    .pNextMenu	= l_DM_List		// local DM list
};

/*================================ Local Data ================================*/

    /*!@brief Actual selection of power output to show times for. */
static int	l_PwrOut;	//!< 0=UA1, 1=UA2, 2=BATT

    /*!@brief Flag if to show overview or detailed time information. */
static bool	l_flgOverview;	//!< true: show overview of power times

    /*!@brief Index within the active detailed view of On/Off times. */
static int	l_MenuIdx;	//!< 0: Power Cycle Interval, 1~n: Alarm Times

    /*!@brief Ranges of On/Off-Times for each power output. */
static const ON_OFF_TIME_RANGE OnOffTimeRange[] =
{
    // #0: On/Off-Time Range for UA1 Power Output
    { "UA1",
      ALARM_UA1_ON_TIME_1,  (ALARM_UA1_OFF_TIME_1 - ALARM_UA1_ON_TIME_1),
      ALARM_UA1_OFF_TIME_1, (ALARM_UA2_ON_TIME_1 - ALARM_UA1_OFF_TIME_1)  },
    // #1: On/Off-Time Range for UA2 Power Output
    { "UA2",
      ALARM_UA2_ON_TIME_1,  (ALARM_UA2_OFF_TIME_1 - ALARM_UA2_ON_TIME_1),
      ALARM_UA2_OFF_TIME_1, (ALARM_BATT_ON_TIME_1 - ALARM_UA2_OFF_TIME_1) },
    // #2: On/Off-Time Range for BATT Power Output
    { "BATT",
      ALARM_BATT_ON_TIME_1,  (ALARM_BATT_OFF_TIME_1 - ALARM_BATT_ON_TIME_1),
      ALARM_BATT_OFF_TIME_1, (NUM_ALARM_IDS - ALARM_BATT_OFF_TIME_1)      },
};

static DISP_MOD DM_PowerTimes_UA1 =
{
    .MenuFct	= MenuPowerTimes,
    .Arg	= 0,			// #0 for UA1
    .DispFct	= DispPowerTimes,
    .pNextMenu	= NULL
};

static DISP_MOD DM_PowerTimes_UA2 =
{
    .MenuFct	= MenuPowerTimes,
    .Arg	= 1,			// #1 for UA2
    .DispFct	= DispPowerTimes,
    .pNextMenu	= NULL
};

static DISP_MOD DM_PowerTimes_BATT =
{
    .MenuFct	= MenuPowerTimes,
    .Arg	= 2,			// #2 for BATT
    .DispFct	= DispPowerTimes,
    .pNextMenu	= NULL
};

static DISP_MOD * const l_DM_List[] =
{
    &DM_PowerTimes_UA1,
    &DM_PowerTimes_UA2,
    &DM_PowerTimes_BATT,
    NULL
};


/***************************************************************************//**
 *
 * @brief	Display On/Off Times for Power Outputs
 *
 * This routine just displays the label of this menu.
 *
 ******************************************************************************/
static void	DispPowerTimesMain (UPD_ID updId)
{
    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    DispPrintf (1, "On/Off-Times for");
	    DispPrintf (2, "Power Outputs");
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Handler for Power Time Menus
 *
 * This menu handler implements all the menus for displaying Power Time entries.
 * There are three menus that give an overview of the configured ON and OFF
 * times for the @ref Power_Outputs UA1, UA2, and BATT.  Below these menus,
 * there are sub-menus which show detailed information, i.e. all time values
 * for each power output.
 *
 * @param[in] keycode
 *	Translated key code, sent by the menu key handler to this module.
 *	If a module cannot handle a key, it must be returned to the key
 *	handler.
 *
 * @param[in] arg
 *	This argument specifies the power output to show information for.
 *	It is 0 for UA1, 1 for UA2, and 2 for the BATT output.
 *
 * @return
 *	If a module cannot handle a key, the key code is returned to the menu
 *	key handler.
 *
 * @warning
 *	This routine is executed in interrupt context!
 *
 * @note
 * This handler can be taken as an example of how to realize "virtual menus",
 * i.e. all sub-menus are implemented within a single menu handler, which
 * decodes the <i>Right-Key</i>, <i>Left-Key</i>, <i>Down-Key</i>, and
 * <i>Up-Key</i> itself to let the display function show the right information.
 *
 ******************************************************************************/
static KEYCODE MenuPowerTimes (KEYCODE keycode, uint32_t arg)
{
static int	MaxIdx;

    l_PwrOut = arg;			// save power channel

    switch (keycode)
    {
	case KEYCODE_MENU_ENTER:	/*---------- Enter this menu ---------*/
	    l_flgOverview = true;	// first show overview about times
	    MaxIdx = OnOffTimeRange[l_PwrOut].OnTimeCount;
	    if (OnOffTimeRange[l_PwrOut].OffTimeCount > MaxIdx)
		MaxIdx = OnOffTimeRange[l_PwrOut].OffTimeCount;

	    /*
	     * NOTE: MaxIdx as an index would be one too high because it
	     *       contains the total COUNT of Alarm times.  Nevertheless
	     *       we have to consider the Power Cycle Interval and On
	     *       Duration as index 0, so MaxIdx is correct already.
	     */
	    break;

	case KEYCODE_DOWN_ASSERT:	/*------------- Next Menu ------------*/
	    if (l_flgOverview)
		break;			// let the upper routine do the job

	    /* detailed view - show next entry of On/Off times */
	    if (++l_MenuIdx > MaxIdx)
		l_MenuIdx = 0;
	    return KEYCODE_MENU_UPDATE;	// show new menu entry

	case KEYCODE_UP_ASSERT:		/*----------- Previous Menu ----------*/
	    if (l_flgOverview)
		break;			// let the upper routine do the job

	    /* detailed view - show previous entry of On/Off times */
	    if (--l_MenuIdx < 0)
		l_MenuIdx = MaxIdx;
	    return KEYCODE_MENU_UPDATE;	// show new menu entry

	case KEYCODE_RIGHT_ASSERT:	/*-------- Enter Detailed View -------*/
	    if (! l_flgOverview)
		return KEYCODE_NONE;	// already detailed view - ignore key

	    l_flgOverview = false;	// leave overview, enter detailed view
	    l_MenuIdx = 0;		// start with index 0
	    return KEYCODE_MENU_UPDATE;	// show new menu

	case KEYCODE_LEFT_ASSERT:	/*-------- Leave Detailed View -------*/
	    if (l_flgOverview)
		break;			// already detailed view - forward key

	    l_flgOverview = true;	// leave detailed, enter overview view
	    return KEYCODE_MENU_UPDATE;	// show new menu

	default:			/*------ Forward all other keys ------*/
	    break;
    }

    return keycode;	// let the calling routine handle the key
}

/***************************************************************************//**
 *
 * @brief	Display Information about ON and OFF Times
 *
 * This routine displays information about the ON and OFF times of the @ref
 * Power_Outputs.  Depending on the current mode, this just gives an overview
 * about the number of times, or shows detailed time values.
 *
 ******************************************************************************/
static void	DispPowerTimes (UPD_ID updId)
{
int	alarm, begin, end;
int	ON_Cnt, OFF_Cnt;
int8_t	hour, min;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	case UPD_CONFIGURATION:	// update when configuration has been changed
	    if (l_flgOverview)
	    {
		/* Overview: count number of ON and OFF Times */
		begin  = OnOffTimeRange[l_PwrOut].OnTimeBase;
		end    = begin + OnOffTimeRange[l_PwrOut].OnTimeCount;
		ON_Cnt = 0;
		for (alarm = begin;  alarm < end;  alarm++)
		{
		    if (AlarmIsEnabled(alarm))
			ON_Cnt++;
		}

		begin  = OnOffTimeRange[l_PwrOut].OffTimeBase;
		end    = begin + OnOffTimeRange[l_PwrOut].OffTimeCount;
		OFF_Cnt = 0;
		for (alarm = begin;  alarm < end;  alarm++)
		{
		    if (AlarmIsEnabled(alarm))
			OFF_Cnt++;
		}

		DispPrintf (1, "%-6s ON :%2d",
			    OnOffTimeRange[l_PwrOut].PowerOutName, ON_Cnt);
		DispPrintf (2, "Times  OFF:%2d", OFF_Cnt);
	    }
	    else if (l_MenuIdx == 0)
	    {
		/* Menu Index 0 displays Power Cycle Interval and Duration */
		if (g_PwrInterval[l_PwrOut] < 0)
		{
		    DispPrintf (1, "%s Power Cycle",
				OnOffTimeRange[l_PwrOut].PowerOutName);
		    DispPrintf (2, "Config-Error");
		}
		else if (g_PwrInterval[l_PwrOut] == 0)
		{
		    DispPrintf (1, "%s Power Cycle",
				OnOffTimeRange[l_PwrOut].PowerOutName);
		    DispPrintf (2, "is disabled");
		}
		else
		{
		    DispPrintf (1, "%-4s Intvl:%4lds",
				OnOffTimeRange[l_PwrOut].PowerOutName,
				g_PwrInterval[l_PwrOut]);
		    DispPrintf (2, "OnDuration:%4lds",
				g_On_Duration[l_PwrOut]);
		}
	    }
	    else
	    {
		/* Display detailed information about each ON and OFF Time */
		alarm = OnOffTimeRange[l_PwrOut].OnTimeBase + l_MenuIdx - 1;
		if (l_MenuIdx > OnOffTimeRange[l_PwrOut].OnTimeCount)
		{
		    /* Index is beyond valid range - no ON time */
		    DispPrintf (1, "%-5s",
				OnOffTimeRange[l_PwrOut].PowerOutName);
		}
		else if (AlarmIsEnabled(alarm))
		{
		    AlarmGet(alarm, &hour, &min);
		    DispPrintf (1, "%-5s ON  %02d:%02d",
				OnOffTimeRange[l_PwrOut].PowerOutName,
				hour, min);
		}
		else
		{
		    DispPrintf (1, "%-5s ON  --:--",
				OnOffTimeRange[l_PwrOut].PowerOutName);
		}

		alarm = OnOffTimeRange[l_PwrOut].OffTimeBase + l_MenuIdx - 1;
		if (l_MenuIdx > OnOffTimeRange[l_PwrOut].OffTimeCount)
		{
		    /* Index is beyond valid range - no OFF time */
		    DispPrintf (2, "%d", l_MenuIdx);
		}
		else if (AlarmIsEnabled(alarm))
		{
		    AlarmGet(alarm, &hour, &min);
		    DispPrintf (2, "%d     OFF %02d:%02d",
				l_MenuIdx, hour, min);
		}
		else
		{
		    DispPrintf (2, "%d     OFF --:--", l_MenuIdx);
		}
	    }
	    break;

	default:
	    break;
    }
}
