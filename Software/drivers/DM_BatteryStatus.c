/***************************************************************************//**
 * @file
 * @brief	Display Module: Battery Status
 * @author	Ralf Gerhauser
 * @version	2018-03-17
 *
 * This display module shows information about the current battery status.
 *
 * Menu Structure:
 @verbatim
----[Battery Status]---+--[Remain. Capacity]
    [13.3V     12mA]   |  [        7044mAh ]
                       |
                       +--[Runtime to empty]
                       |  [   >  6 weeks   ]
                       |
                       +--[Manufacturer    ]
                       |  [ Dynamis        ]
                       |
                       +--[Device Name     ]
                       |  [ BMS2-4_V0.3    ]
                       |
                       +--[Device Type     ]
                       |  [ Lithium-Ion    ]
                       |
                       +--[Serial Number   ]
                       |  [          00008 ]
                       |
                       +--[Design Capacity ]
                       |  [        7500mAh ]
                       |
                       +--[Full Charge Cap.]
                          [        8558mAh ]
 @endverbatim
 *
 ****************************************************************************//*
Revision History:
2018-03-17,rage	Initial version, based on project "AlarmClock".
*/

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <string.h>
#include "em_device.h"
#include "em_assert.h"
#include "clock.h"		// include g_rtcStartTime
#include "AlarmClock.h"
#include "DisplayMenu.h"
#include "BatteryMon.h"
#include "DM_BatteryStatus.h"

/*=========================== Forward Declarations ===========================*/

static void	DispBatteryStatus (UPD_ID updId);
static void	DispBatteryInfo1 (UPD_ID updId);
static void	DispBatteryInfo2 (UPD_ID updId);
static void	DispBatteryInfo3 (UPD_ID updId);
static void	DispBatteryInfo4 (UPD_ID updId);
static void	DispBatteryInfo5 (UPD_ID updId);
static void	DispBatteryInfo6 (UPD_ID updId);
static void	DispBatteryInfo7 (UPD_ID updId);
static void	DispBatteryInfo8 (UPD_ID updId);

static DISP_FCT const l_DispFctList[];

/*================================ Global Data ===============================*/

/*! Module to display the current battery status and information. */
DISP_MOD DM_BatteryStatus =
{
    .MenuFct	= MenuDistributor,
    .Arg	= 0,
    .DispFct	= DispBatteryStatus,
    .pNextMenu	= (SIMPLE_MENU)l_DispFctList
};

/*================================ Local Data ================================*/

/* A simple menu consists only of a list of display functions */
static DISP_FCT const l_DispFctList[] =
{
    NULL,		// MenuFct=NULL identifies a simple menu list
    DispBatteryInfo1,
    DispBatteryInfo2,
    DispBatteryInfo3,
    DispBatteryInfo4,
    DispBatteryInfo5,
    DispBatteryInfo6,
    DispBatteryInfo7,
    DispBatteryInfo8,
    NULL
};

/***************************************************************************//**
 *
 * @brief	Display Battery Status
 *
 * This routine displays the actual battery voltage and current.
 *
 ******************************************************************************/
static void	DispBatteryStatus (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_Voltage, SBS_BatteryCurrent);
	    DispPrintf (1, "Battery Status");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
	    {
		DispPrintf (2, "%2ld.%ldV  %5ldmA",
			    (pBatInfo->Data_1 / 1000),
			    (pBatInfo->Data_1 % 1000) / 100,
			    pBatInfo->Data_2);
	    }
	    BatteryInfoReq (SBS_Voltage, SBS_BatteryCurrent);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 1
 *
 * This routine shows the remaining capacity of the battery in [mAh].
 *
 ******************************************************************************/
static void	DispBatteryInfo1 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_RemainingCapacity, SBS_NONE);
	    DispPrintf (1, "Remain. Capacity");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "      %6dmAh", pBatInfo->Data_1);

	    BatteryInfoReq (SBS_RemainingCapacity, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 2
 *
 * This routine shows the runtime to empty in weeks or days.
 *
 ******************************************************************************/
static void	DispBatteryInfo2 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_RunTimeToEmpty, SBS_NONE);
	    DispPrintf (1, "Runtime to empty");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
	    {
		if (pBatInfo->Data_1 >= 65534)
		    DispPrintf (2, "   >  6 weeks");
		else
		    DispPrintf (2, "%5dmin (%dd)", pBatInfo->Data_1,
				pBatInfo->Data_1 / 60 / 24);
	    }
	    BatteryInfoReq (SBS_RunTimeToEmpty, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 3
 *
 * This routine shows the battery manufacturer.
 *
 ******************************************************************************/
static void	DispBatteryInfo3 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_ManufacturerName, SBS_NONE);
	    DispPrintf (1, "Manufacturer");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "%s", pBatInfo->Buffer);

	    BatteryInfoReq (SBS_ManufacturerName, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 4
 *
 * This routine shows the battery Device Name.
 *
 ******************************************************************************/
static void	DispBatteryInfo4 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_DeviceName, SBS_NONE);
	    DispPrintf (1, "Device Name");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "%s", pBatInfo->Buffer);

	    BatteryInfoReq (SBS_DeviceName, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 5
 *
 * This routine shows the battery Device Type.
 *
 ******************************************************************************/
static void	DispBatteryInfo5 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_DeviceChemistry, SBS_NONE);
	    DispPrintf (1, "Device Type");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "%s", pBatInfo->Buffer);

	    BatteryInfoReq (SBS_DeviceChemistry, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 6
 *
 * This routine shows the Serial Number of the device.
 *
 ******************************************************************************/
static void	DispBatteryInfo6 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_SerialNumber, SBS_NONE);
	    DispPrintf (1, "Serial Number");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "          %05d", pBatInfo->Data_1);

	    BatteryInfoReq (SBS_SerialNumber, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 7
 *
 * This routine shows the Design Capacity of the battery.
 *
 ******************************************************************************/
static void	DispBatteryInfo7 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_DesignCapacity, SBS_NONE);
	    DispPrintf (1, "Design Capacity");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "      %6dmAh", pBatInfo->Data_1);

	    BatteryInfoReq (SBS_DesignCapacity, SBS_NONE);
	    break;

	default:
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Display Battery Information - Part 8
 *
 * This routine shows the Full Charge Capacity of the battery.
 *
 ******************************************************************************/
static void	DispBatteryInfo8 (UPD_ID updId)
{
BAT_INFO *pBatInfo;

    switch (updId)
    {
	case UPD_ALL:		// initial write to display buffer
	    BatteryInfoReq (SBS_FullChargeCapacity, SBS_NONE);
	    DispPrintf (1, "Full Charge Cap.");
	    DispPrintf (2, "");
	    /* no break */

	case UPD_SYSCLOCK:	// update values with SYSCLOCK (every second)
	    pBatInfo = BatteryInfoGet();
	    if (pBatInfo->Done)
		DispPrintf (2, "      %6dmAh", pBatInfo->Data_1);

	    BatteryInfoReq (SBS_FullChargeCapacity, SBS_NONE);
	    break;

	default:
	    break;
    }
}
