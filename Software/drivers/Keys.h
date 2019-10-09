/***************************************************************************//**
 * @file
 * @brief	Header file of module Keys.c
 * @author	Ralf Gerhauser
 * @version	2018-02-19
 ****************************************************************************//*
Revision History:
2018-02-19,rage	Added functionality from menu control project.
2016-04-13,rage	Removed KEY_TRIG_MASK (no more required by EXTI module).
2014-11-11,rage	Derived from project "AlarmClock".
*/

#ifndef __INC_Keys_h
#define __INC_Keys_h

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <stdbool.h>
#include "em_device.h"
#include "em_gpio.h"
#include "config.h"		// include project configuration parameters

/*=============================== Definitions ================================*/

/*!@brief Per default the key autorepeat function is enabled, however for
 * debugging purposes, it may be useful to disable it by setting the define
 * to 0.
 */
#ifndef KEY_AUTOREPEAT
  #define KEY_AUTOREPEAT	1
#endif

/*!@brief Here follows the definition of all keys (push buttons) and their
 * related hardware configurations.
 */
#define KEY_UP_PORT	gpioPortA
#define KEY_UP_PIN	8

#define KEY_DOWN_PORT	gpioPortC
#define KEY_DOWN_PIN	13

#define KEY_RIGHT_PORT	gpioPortA
#define KEY_RIGHT_PIN	9

#define KEY_LEFT_PORT	gpioPortA
#define KEY_LEFT_PIN	10

#define KEY_SET_PORT	gpioPortC
#define KEY_SET_PIN	14


/*!@brief Bit mask of all affected external interrupts (EXTIs). */
#define KEY_EXTI_MASK	((1 << KEY_UP_PIN)    | (1 << KEY_DOWN_PIN) |	\
			 (1 << KEY_RIGHT_PIN) | (1 << KEY_LEFT_PIN) |	\
			 (1 << KEY_SET_PIN))

/*=========================== Typedefs and Structs ===========================*/

/*!@brief Translated key codes. */
typedef enum
{
    KEYCODE_NONE,		//!< No key code active
    KEYCODE_UP_ASSERT,		//!< Key code for UP once asserted
    KEYCODE_UP_REPEAT,		//!< Key code for UP autorepeat, still active
    KEYCODE_UP_RELEASE,		//!< Key code for UP released again
    KEYCODE_DOWN_ASSERT,	//!< Key code for DOWN once asserted
    KEYCODE_DOWN_REPEAT,	//!< Key code for DOWN autorepeat, still active
    KEYCODE_DOWN_RELEASE,	//!< Key code for DOWN released again
    KEYCODE_RIGHT_ASSERT,	//!< Key code for RIGHT once asserted
    KEYCODE_RIGHT_REPEAT,	//!< Key code for RIGHT autorepeat, still active
    KEYCODE_RIGHT_RELEASE,	//!< Key code for RIGHT released again
    KEYCODE_LEFT_ASSERT,	//!< Key code for LEFT once asserted
    KEYCODE_LEFT_REPEAT,	//!< Key code for LEFT autorepeat, still active
    KEYCODE_LEFT_RELEASE,	//!< Key code for LEFT released again
    KEYCODE_SET_ASSERT,		//!< Key code for SET once asserted
    KEYCODE_SET_REPEAT,		//!< Key code for SET autorepeat, still active
    KEYCODE_SET_RELEASE,	//!< Key code for SET released again
    /*! there follow some pseudo key codes for the menus */
    KEYCODE_MENU_INIT,		//!< Initialization of a menu module
    KEYCODE_MENU_ENTER,		//!< Enter the specified menu
    KEYCODE_MENU_EXIT,		//!< Exit the menu
    KEYCODE_MENU_UPDATE,	//!< Update current menu data (e.g. time)
    END_KEYCODE			//!< End of key code definitions
} KEYCODE;

/*!@brief Offsets to be added to the ASSERT key code */
#define KEYOFFS_REPEAT	(KEYCODE)1	// +1 for REPEAT code
#define KEYOFFS_RELEASE	(KEYCODE)2	// +2 for RELEASE code

/*!@brief Function to be called for each translated key code. */
typedef void	(* KEY_FCT)(KEYCODE keycode);

/*!@brief Key initialization structure.
 *
 * Initialization structure to define the timings for the autorepeat (AR)
 * threshold and rate (in milliseconds), and a function to be called for each
 * translated key.
 *
 * <b>Typical Example:</b>
 * @code
 * static const KEY_INIT l_KeyInit =
 * {
 *    250,		// Autorepeat threshold is 250ms
 *    100,		// Autorepeat rate is 100ms (10Hz)
 *    MenuKeyHandler	// Key handler of module "Menu.c"
 * };
 * @endcode
 */
typedef struct
{
    uint16_t  AR_Threshold;	//!< Threshold in [ms] after autorepeat starts
    uint16_t  AR_Rate;		//!< Key rate in [ms] when autorepeat is active
    KEY_FCT   KeyFct;		//!< Fct. to be called for each translated key
} KEY_INIT;

/*================================ Prototypes ================================*/

/* Initialize key hardware */
void	KeyInit (const KEY_INIT *pInitStruct);

/* Key handler, called from interrupt service routine */
void	KeyHandler	(int extiNum, bool extiLvl, uint32_t timeStamp);


#endif /* __INC_Keys_h */
