/***************************************************************************//**
 * @file
 * @brief	Header file of module DisplayMenu.c
 * @author	Ralf Gerhauser
 * @version	2014-04-10
 ***************************************************************************//*
Revision History:
2014-04-10,rage	Initial version.
*/

#ifndef __INC_DisplayMenu_h
#define __INC_DisplayMenu_h

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <stdbool.h>
#include "em_device.h"
#include "config.h"		// include project configuration parameters
#include "Keys.h"

/*=============================== Definitions ================================*/

    /*!@brief Time in [s] after which the LCD is powered-off. */
#ifndef LCD_POWER_OFF_TIMEOUT
    #define LCD_POWER_OFF_TIMEOUT	30
#endif

/*=========================== Typedefs and Structs ===========================*/

/*!@brief Identifiers to tell which values have been updated. */
typedef enum
{
    UPD_ALL,			//!< Force update of currently displayed items
    UPD_CONFIGURATION,		//!< Configuration variables have been changed
    UPD_SYSCLOCK,		//!< System Clock (time, date) was updated
    UPD_TRANSPONDER,		//!< Transponder number was updated
    UPD_POWERSTATUS,		//!< Power output has been changed
    END_UPD_ID			//!< End of list of these update identifiers
} UPD_ID;


/*!@brief Function to be called for each translated key code. */
typedef KEYCODE	(* MENU_FCT)(KEYCODE keycode, uint32_t arg);

/*!@brief Function to be called to (re-)display data on the LCD. */
typedef void	(* DISP_FCT)(UPD_ID updId);

/*!@brief Definition of a display module.
 *
 * A display module (DM) provides all the functionality to display text and
 * data on the LCD.  It always uses both lines of the display.
 * This structure keeps all the relevant information of a DM), this is:
 *
 * @param MenuFct
 * 	Function is called for every translated key code.  It navigates through
 * 	the menu structure.  The are several pseudo key codes to communicate
 * 	between the MenuKeyHandler() and this kind of routines.
 *
 * @param Arg
 *	Optional argument for the menu function.  This allows to use the same
 *	menu function for several
 * @param DispFct
 * 	Function is called when this display module is selected (active)
 * 	to initially display the respective data.  Also called, when data is
 * 	updated and need to be re-displayed on the LCD.
 *
 * @param pNextMenu
 * 	Pointer to another list of menu entries.  This will be activated when
 * 	the <i>Right-Key</i> is asserted.  If there exists no further menu,
 * 	the pointer is NULL.
 */
typedef const struct _DM_
{
    MENU_FCT	MenuFct;	//!< Menu function to be called for each key
    uint32_t	Arg;		//!< Optional argument for the menu module
    DISP_FCT	DispFct;	//!< Display function writes data to the LCD
    const struct _DM_ * const *pNextMenu; //!< Ptr to next array of menu entries
} DISP_MOD;

/*!@brief Type cast to introduce a simplified version of a display module.
 *
 * This is an array of function pointers of type @ref DISP_FCT, with element 0,
 * which is interpreted as @ref DISP_MOD.MenuFct entry, set to NULL to identify
 * the list as simple menu.  The list of display functions starts with element
 * 1 and is finally terminated with another NULL pointer.  Simple menus only
 * provide display function, they do not have any menu function.
 */
typedef const struct _DM_ * const *	SIMPLE_MENU;


/*!@brief Function to be called when specified duration is over
 *
 * This type of function is used by DisplayNext() to install a callback
 * routine which is executed after the specified amount of time has elapsed.
 * The function argument <b>userParm</b> can be used to specify the next
 * item to be displayed.
 */
typedef void	(* DISP_NEXT_FCT)(int userParm);

/*================================ Prototypes ================================*/

/* Initialize Menus */
void	MenuInit (DISP_MOD * const pMenuList[]);

/* Menu Key Handler, called from interrupt service routine */
void	MenuKeyHandler (KEYCODE keycode);

/* Check if Home Screen is active */
bool	IsHomeScreen (void);

/* Restart or Cancel Display Timer */
void	DisplayTimerRestart (void);
void	DisplayTimerCancel (void);

/* checks if the information on the LC-Display needs to be updated */
void	DisplayUpdateCheck (void);

/* generic Display Update routine */
void	DisplayUpdate (UPD_ID updId);

/* DCF77 enables Display Update when clock has been synchronized */
void	DisplayUpdEnable (void);

/* display print formatted to LCD */
void	DispPrintf (int lineNum, const char *frmt, ...);

/* display text on the LCD */
void	DisplayText (int lineNum, const char *frmt, ...);

/* defines what should happen next on the LC-Display */
void	DisplayNext (unsigned int duration, DISP_NEXT_FCT fct, int userParm);

/* Display Update for Clock */
void	DisplayUpdateClock (void);

/* Generic Menu Handler - leads to further menu entries */
KEYCODE MenuDistributor (KEYCODE keycode, uint32_t arg);

#endif /* __INC_DisplayMenu_h */
