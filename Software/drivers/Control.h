/***************************************************************************//**
 * @file
 * @brief	Header file of module Control.c
 * @author	Ralf Gerhauser
 * @version	2018-10-10
 ****************************************************************************//*
Revision History:
2018-10-10,rage	Added prototype VerifyConfiguration(), removed unused prototypes.
		Added timing variables for Power Cycling.
2018-03-26,rage	Initial version, based on MAPRDL.
*/

#ifndef __INC_Control_h
#define __INC_Control_h

/*=============================== Header Files ===============================*/

#include "config.h"		// include project configuration parameters

/*=============================== Definitions ================================*/

    /*!@brief Show module "Control" exists in this project. */
#define MOD_CONTROL_EXISTS

#ifndef DFLT_SCAN_DURATION
    /*!@brief Default scan duration in [ms] for one of four channels. */
    #define DFLT_SCAN_DURATION		1000	// 1000ms
#endif

#ifndef DFLT_MEASURE_U_MIN_DIFF
    /*!@brief Default minimum difference for a new U-value (in mV). */
    #define DFLT_MEASURE_U_MIN_DIFF	100	// 100mV
#endif

#ifndef DFLT_MEASURE_I_MIN_DIFF
    /*!@brief Default minimum difference for a new I-value (in mA). */
    #define DFLT_MEASURE_I_MIN_DIFF	10	// 10mA
#endif

#ifndef DFLT_MEASURE_FOLLOW_UP_TIME
    /*!@brief Default follow-up time for measuring U/I (in seconds). */
    #define DFLT_MEASURE_FOLLOW_UP_TIME	(1*60)	// 1min
#endif

    /*!@brief Power output selection. */
typedef enum
{
    PWR_OUT_NONE = NONE,	// (-1) for no output at all
    PWR_OUT_UA1,		// 0: DC/DC (3V3 to 12V) at pin UA1 (X2-5)
    PWR_OUT_UA2,		// 1: DC/DC (3V3 to 12V) at pin UA2 (X2-7)
    PWR_OUT_BATT,		// 2: 12V at pin BATT_OUTPUT (X2-3)
    NUM_PWR_OUT
} PWR_OUT;

    /*!@brief Power control. */
//@{
#define PWR_OFF		false	//!< Switch power output off (disable power)
#define PWR_ON		true	//!< Switch power output on  (enable power)
//@}

/*================================ Global Data ===============================*/

extern const char *g_enum_PowerOutput[];
extern int32_t     g_PwrInterval[NUM_PWR_OUT];
extern int32_t     g_On_Duration[NUM_PWR_OUT];

/*================================ Prototypes ================================*/

    /* Initialize control module */
void	ControlInit (void);

    /* Clear Configuration variables (set default values) */
void	ClearConfiguration (void);

    /* Verify Configuration values */
void	VerifyConfiguration (void);

    /* Perform miscellaneous control tasks */
void	Control (void);

    /* Inform the control module about a new transponder ID */
void	ControlUpdateID (char *transponderID);

    /* Switch power output on or off */
void	PowerOutput	(PWR_OUT output, bool enable);
bool	IsPowerOutputOn (PWR_OUT output);

    /* Power Fail Handler of the control module */
void	ControlPowerFailHandler (void);

    /* Get actual Voltage and Current of a power output */
uint32_t PowerVoltage (PWR_OUT output);
uint32_t PowerCurrent (PWR_OUT output);

    /* Get ADC raw value for a given voltage in [mV] or current in [mA] */
uint32_t Voltage_To_ADC_Value (PWR_OUT output, uint32_t value_mV);
uint32_t Current_To_ADC_Value (PWR_OUT output, uint32_t value_mA);

    /* Calibrate Voltage and Current measurement and write data to flash */
void	CalibrateVoltage (PWR_OUT output, uint32_t referenceValue_mV);
void	CalibrateCurrent (PWR_OUT output, uint32_t referenceValue_mA);
void	WriteCalibrationData(void);


#endif /* __INC_Control_h */
