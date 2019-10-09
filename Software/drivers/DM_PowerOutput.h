/***************************************************************************//**
 * @file
 * @brief	Header file of display module DM_PowerOutput.c
 * @author	Ralf Gerhauser
 * @version	2018-03-14
 ****************************************************************************//*
Revision History:
2018-03-14,rage	Initial version.
*/

#ifndef __INC_DM_PowerOutput_h
#define __INC_DM_PowerOutput_h

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <stdbool.h>
#include "em_device.h"
#include "config.h"		// include project configuration parameters
#include "DisplayMenu.h"

/*================================ External Data =============================*/

extern DISP_MOD DM_PowerStatus;	// Power status, measurement, and switching
extern DISP_MOD DM_Calibration;	// Calibration menu for UA1 and UA2 measurement

extern uint32_t g_UA_Calib_mV[2];
extern uint32_t g_UA_Calib_mA[2];


#endif /* __INC_DM_PowerOutput_h */
