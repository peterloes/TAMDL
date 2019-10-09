/***************************************************************************//**
 * @file
 * @brief	Display and Menu Manager
 * @author	Ralf Gerhauser
 * @version	2018-03-26
 *
 * This is the Display and Menu Manager.  It controls all the information on
 * the LC-Display, including Menus.<br>
 *
 * In detail, it can be used for the following tasks:
 * - Display useful run-time information, like time, battery voltage, current,
 *   and capacity.
 * - Allow the user to interact with the system, e.g. switch power outputs,
 *   or calibrate voltage and current measurement.
 *
 * Document <a href="../TAMDL_Menu_Description.pdf">TAMDL Menu Description</a>
 * provides you an overview of the available menus.
 *
 ***************************************************************************//*
Revision History:
2018-03-26,rage	Simplified menu handling:  A Display Module (DM) always uses
		both lines of the LC-Display.  There exists no extra menu and
		display mode anymore.  Simple Menus (SIMPLE_MENU) allow you to
		specify a list of pure display functions, without the overhead
		of menu functions or Display Modules.
		While the creation and update of information may happen in
		interrupt context, the data transfer to the LCD is always done
		from the main loop via function DisplayUpdateCheck().
2014-04-10,rage	Initial version.
*/

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <stdarg.h>
#include "em_device.h"
#include "em_assert.h"
#include "Logging.h"
#include "Keys.h"
#include "AlarmClock.h"
#include "DisplayMenu.h"
#include "LCD_DOGM162.h"	// LCD_Init(), LCD_PowerOn(), LCD_PowerOff()

/*=============================== Definitions ================================*/

    /*!@brief Maximum number of menu levels on the stack. */
#define MAX_MENU_LEVEL		5

/*=========================== Forward Declarations ===========================*/

static void	CopyBufferToLCD (void);

/*================================ Global Data ===============================*/


/*================================ Local Data ================================*/

    /*!@brief Line buffers to store text during menu is active. */
static char l_LineBuffer[2][LCD_DIMENSION_X+1];	// EOS needs another byte

    /*!@brief Pointer to a list of display modules (first level). */
static DISP_MOD * const *l_pMenuMainList;

    /*!@brief Pointer to the current list of display modules. */
static DISP_MOD * const *l_pMenuCurrList;

    /*!@brief Stack to save the indices for the lists of display modules. */
static uint8_t	 l_MenuIdxStack[MAX_MENU_LEVEL];

    /*!@brief Index (level) within the @ref l_MenuIdxStack. */
static uint8_t	 l_MenuIdxStackLevel;

    /*!@brief Flag if this is a simple menu, see @ref SIMPLE_MENU. */
static bool	 l_SimpleMenu;

    /*!@brief Timer handle for switching the display off after a time. */
static volatile TIM_HDL l_hdlLCD_Off = NONE;

    /*!@brief Timer handle for Display Next callback routine. */
static volatile TIM_HDL l_hdlDispNext = NONE;

    /*!@brief Flag if Display is currently powered on. */
static volatile bool	l_flgDisplayIsOn;

    /*!@brief Flag if Display should be powered on. */
static volatile bool	l_flgDisplayOn;

    /*!@brief Flags if data needs to be written to the LCD.
     * Bits 0 and 1 are used to copy the buffer for line 1 and 2 respectively.
     * Bits 2 and 3 protect the line buffers to be overwritten when text has
     * been stored already by function DisplayText().
     */
static volatile uint8_t	l_flgCopyBufferToLCD;

    /*!@brief Flag is set after DCF77 Date and Time has been displayed. */
static volatile bool	l_flgDisplayUpdEnabled;

    /*!@brief Flag triggers the execution of function @ref l_DispNextFct. */
static volatile bool	l_DispNextFctTrigger;

    /*!@brief Function pointer for a callback routine which is executed after
     * the specified amount of time has elapsed, see @ref DisplayNext().
     */
static volatile DISP_NEXT_FCT l_DispNextFct;

    /*!@brief User parameter for function @ref l_DispNextFct. */
static volatile int	l_DispNextUserParm;

/*=========================== Forward Declarations ===========================*/

static void SwitchLCD_Off(TIM_HDL hdl);
static void DispNextTrigger(TIM_HDL hdl);


/***************************************************************************//**
 *
 * @brief	Initialize Menus
 *
 * This routine must be called to introduce a list of menu modules.
 *
 * @param[in] pMenuMainList
 *	Pointer to a list of display modules for the main menu.  See
 *	@ref DISP_MOD for more information about Display Modules (DM).
 *
 * @note
 *	Parameter <b>pMenuList</b> must point to a persistent data structure,
 *	i.e. this must be valid over the whole life time of the program.
 *
 ******************************************************************************/
void	MenuInit (DISP_MOD * const pMenuMainList[])
{
int	i;		// index variable


    /* Parameter check */
    EFM_ASSERT(pMenuMainList != NULL);

    /* Save configuration and set current menu list */
    l_pMenuCurrList = l_pMenuMainList = pMenuMainList;

    /* Initialize all Main Menu modules in the list */
    for (i = 0;  pMenuMainList[i] != NULL;  i++)
    {
	/* may be used, e.g. to load parameters from a file */
	l_pMenuMainList[i]->MenuFct (KEYCODE_MENU_INIT, pMenuMainList[i]->Arg);
    }

    /* Get a timer handle to switch the display off after a time */
    if (l_hdlLCD_Off == NONE)
	l_hdlLCD_Off = sTimerCreate (SwitchLCD_Off);

    /* Create timer to trigger a callback routine after duration is over */
    if (l_hdlDispNext == NONE)
	l_hdlDispNext = sTimerCreate (DispNextTrigger);

    /* Connect the update function */
    DisplayUpdateFctInstall (DisplayUpdateClock);

    /* Set flags to active state */
    l_flgDisplayOn = l_flgDisplayIsOn = true;

    /* Initialize the LCD module specific parts */
    LCD_Init();
}


/***************************************************************************//**
 *
 * @brief	Display Update Check
 *
 * This function checks if the information on the LC-Display needs to be
 * updated, or if the LCD is currently not used and can be switched off.
 * When switching of the LC-Display, this implies exiting the current menu
 * and go back to the default (home) screen.
 *
 * @note
 * 	This function may only be called from standard program, usually the loop
 * 	in module "main.c" - it must not be called from interrupt routines!
 *
 ******************************************************************************/
void	DisplayUpdateCheck (void)
{
    /*
     * Check for callback trigger first
     */
    if (l_DispNextFctTrigger)
    {
	DISP_NEXT_FCT fct = l_DispNextFct;

	/* Clear trigger flag */
	l_DispNextFctTrigger = false;

	/* See if a callback routine has been defined and call it */
	if (fct)
	{
	    l_DispNextFct = NULL;	// no NEW callback for default

	    fct (l_DispNextUserParm);	// call user routine
	}
	else
	{
	    /* No callback - switch LCD off if HOME screen is active*/
	    if (IsHomeScreen())
		SwitchLCD_Off((TIM_HDL)0);
	}

    }

    /*
     * Check if LC-Display should be powered-on or off.  This is executed
     * in this main loop since it must not happen in any interrupt service
     * routine (ISR) due to calling delay functions and other issues.
     * However, the reason when to do it is triggered via ISRs.
     */
    if (l_flgDisplayOn)
    {
	/* LCD should be powered ON */
	if (! l_flgDisplayIsOn)
	{
	    LCD_PowerOn();
	    l_flgDisplayIsOn = true;

	    /* LCD is ON now - check if fields need to be updated */
	    DisplayUpdate (UPD_ALL);
	}

	/* See if data needs to be written to the LCD */
	if (l_flgCopyBufferToLCD)
	{
	    CopyBufferToLCD();
	}
    }
    else
    {
	/* LCD should be powered OFF */
	if (l_flgDisplayIsOn)
	{
	    /*
	     * Call handler as if it would be called by SET key.  This exits
	     * the current menu (maybe a clean-up is required before powering
	     * off the LC-Display) and returns to the default (home) screen.
	     */
	    MenuKeyHandler (KEYCODE_SET_REPEAT);

	    LCD_PowerOff();
	    l_flgDisplayIsOn = false;
	}
    }
}


/***************************************************************************//**
 *
 * @brief	Restart Display Timer
 *
 * Call this function to restart the display timer.  After the timer has expired
 * the display will be switched off.
 *
 ******************************************************************************/
void	DisplayTimerRestart (void)
{
	if (l_hdlLCD_Off != NONE)
	    sTimerStart (l_hdlLCD_Off, LCD_POWER_OFF_TIMEOUT);
}


/***************************************************************************//**
 *
 * @brief	Cancel Display Timer
 *
 * Call this function to cancel the display timer.  This is used to keep the
 * display switched on until the timer is restarted again, e.g. by asserting
 * a key.
 *
 ******************************************************************************/
void	DisplayTimerCancel (void)
{
    if (l_hdlLCD_Off != NONE)
	sTimerCancel (l_hdlLCD_Off);
}


/***************************************************************************//**
 *
 * @brief	Menu Key Handler
 *
 * This handler receives the translated key codes from the interrupt-driven
 * key handler, including autorepeat keys.  That is, whenever the user asserts
 * a key (push button), the resulting code is sent to this function.  The main
 * purpose of the handler is to navigate through the menu structure which has
 * been defined previously via function MenuInit().
 *
 * The following keys are recognized:
 * - <b>DOWN</b> selects the next display module.  This key provides
 *   autorepeat functionality, i.e. it is repeated when permanently asserted.
 * - <b>UP</b> selects the previous display module.  Autorepeat lets you
 *   effectively move through the menus in a fast way.
 * - <b>RIGHT</b> enters the next deeper level of sub-menus.
 * - <b>LEFT</b> leaves the current sub-menu and returns to the menu one level
 *   above.
 * - <b>SET</b> is used by some menus to trigger an action, e.g. to enable or
 *   disable a Power Output.  Asserting SET for more than 750ms exits the
 *   current menu, returns to the home screen and switches the LCD off.
 *
 * The duration how long the respective information is displayed before
 * switching the LCD off again, can be adjusted by the define @ref
 * LCD_POWER_OFF_TIMEOUT.
 *
 * @warning
 * 	This function is called in interrupt context!
 *
 * @param[in] keycode
 *	Translated key code of type KEYCODE.
 *
 * @see
 * 	KeyHandler()
 *
 ******************************************************************************/
void	MenuKeyHandler (KEYCODE keycode)
{
KEYCODE		retKeyCode;		// returned key code
int		idx;			//  current index within menu list
static bool	flgSetKeyAsserted;	// true if SET key is still asserted


    /* Verify if menu list exists */
    EFM_ASSERT(l_pMenuCurrList != NULL);

    /* If SET key is asserted for more than 750ms, the menu returns to home */
    if (keycode == KEYCODE_SET_REPEAT)
    {
	if (flgSetKeyAsserted)
	    return;			// ignore further SET key codes

	/* must be a new assertion of SET key - EXIT menu */
	flgSetKeyAsserted = true;	// SET key is asserted

	/* Exit current menu, return to default screen */
	keycode = KEYCODE_MENU_EXIT;
    }
    else
    {
	flgSetKeyAsserted = false;	// other key - clear SET flag

	/* First assertion of SET-Key also triggers a LogFlush() */
	if (keycode == KEYCODE_SET_ASSERT)
	    LogFlushTrigger();

	/*
	 * Any other key retriggers the timer to switch OFF the
	 * display after some time.
	 */
	DisplayTimerRestart();

	/* See if LC-Display is currently off */
	if (! l_flgDisplayOn)
	{
	    l_flgDisplayOn = true;
	    return;	// ignore first key assertion - just power-on LCD
	}
    }

    /* Get current index within current menu list */
    idx = l_MenuIdxStack[l_MenuIdxStackLevel];

    /* First call menu function with key code and argument */
    if (l_SimpleMenu)
	retKeyCode = keycode;	// no MenuFct, just pass key code
    else
	retKeyCode = l_pMenuCurrList[idx]->MenuFct (keycode,
						    l_pMenuCurrList[idx]->Arg);

    if (flgSetKeyAsserted)
    {
	/* SET key is asserted to go back to the default (home) screen */
	l_pMenuCurrList = l_pMenuMainList;
	l_MenuIdxStack[0] = 0;
	l_MenuIdxStackLevel = 0;
	l_SimpleMenu = false;
    }

    /* Check return key code */
    switch (retKeyCode)
    {
	case KEYCODE_MENU_ENTER:	// New menu display has been entered
	case KEYCODE_MENU_EXIT:		// Previous menu was exited
	case KEYCODE_MENU_UPDATE:	// Update LC-Display
	    DisplayUpdate (UPD_ALL);	// Force update, independent of UpdId
	    break;


	case KEYCODE_UP_ASSERT:		// Move to previous menu
	case KEYCODE_UP_REPEAT:
	case KEYCODE_DOWN_ASSERT:	// Move to next menu
	case KEYCODE_DOWN_REPEAT:
	    /* exit current menu */
	    if (! l_SimpleMenu)
		l_pMenuCurrList[idx]->MenuFct (KEYCODE_MENU_EXIT,
					       l_pMenuCurrList[idx]->Arg);
	    /* move to next/previous menu */
	    if (retKeyCode == KEYCODE_DOWN_ASSERT
	    ||  retKeyCode == KEYCODE_DOWN_REPEAT)
	    {
		/* activate next menu in list */
		if (l_pMenuCurrList[idx + 1] == NULL)
		    idx = 0;		// reached end, wrap around
		else
		    idx++;		// next menu
	    }
	    else
	    {
		/* activate previous menu in list */
		if (idx == 0)
		{
		    /* already position 0, search end of list */
		    while (l_pMenuCurrList[idx + 1] != NULL)
			idx++;
		}
		else
		{
		    idx--;		// previous menu
		}
	    }
	    /* save new index */
	    l_MenuIdxStack[l_MenuIdxStackLevel] = idx;

	    /* enter new menu by recursively calling this handler */
	    MenuKeyHandler (KEYCODE_MENU_ENTER);
	    break;


	case KEYCODE_LEFT_ASSERT:	// Leave menu list, return to previous
	    /* check if already main menu */
	    if (l_MenuIdxStackLevel == 0)
		break;			// already main menu, ignore key

	    /* exit current menu */
	    if (! l_SimpleMenu)
		l_pMenuCurrList[idx]->MenuFct (KEYCODE_MENU_EXIT,
					       l_pMenuCurrList[idx]->Arg);

	    /* move back to previous menu list */
	    l_MenuIdxStackLevel--;
	    l_pMenuCurrList = l_pMenuMainList;
	    for (idx = 0;  idx < l_MenuIdxStackLevel;  idx++)
	    {
		l_pMenuCurrList = l_pMenuCurrList[l_MenuIdxStack[idx]]->pNextMenu;
		if (l_pMenuCurrList == NULL)
		{
		    Log ("INTERNAL ERROR: Menu Level %d Idx %d next menu is NULL",
			 idx, l_MenuIdxStack[idx]);
		    l_pMenuCurrList = l_pMenuMainList;
		    l_MenuIdxStack[0] = 0;
		    l_MenuIdxStackLevel = 0;	// select HOME entry
		    break;
		}
	    }

	    l_SimpleMenu = false;	// a level higher can't be a simple menu

	    /* enter new menu by recursively calling this handler */
	    MenuKeyHandler (KEYCODE_MENU_ENTER);
	    break;


	case KEYCODE_RIGHT_ASSERT:	// Leave menu list, go to next list
	    /* check if maximum menu level has been reached */
	    if (l_MenuIdxStackLevel == MAX_MENU_LEVEL - 1)
	    {
		Log ("INTERNAL ERROR: Reached maximum menu level %d",
		     MAX_MENU_LEVEL);
		break;			// ignore key
	    }

	    /* check if it is a simple menu (does not have a next menu) */
	    if (l_SimpleMenu)
		break;

	    /* check if there exists another menu list */
	    if (l_pMenuCurrList[idx]->pNextMenu == NULL)
		break;			// no, ignore key

	    /* exit current menu */
	    l_pMenuCurrList[idx]->MenuFct (KEYCODE_MENU_EXIT,
					   l_pMenuCurrList[idx]->Arg);
	    /* activate next menu list */
	    l_pMenuCurrList = l_pMenuCurrList[idx]->pNextMenu;
	    l_MenuIdxStack[++l_MenuIdxStackLevel] = 0;

	    /* check if this is a "simple menu" */
	    if (l_pMenuCurrList[0] == NULL)
	    {
		l_pMenuCurrList++;	// real start of DISP_FCT array
		l_SimpleMenu = true;
	    }

	    /* enter new menu by recursively calling this handler */
	    MenuKeyHandler (KEYCODE_MENU_ENTER);
	    break;

	default:			// ignore all other key codes
	    break;
    }
}


/***************************************************************************//**
 *
 * @brief	Check if Home Screen is active
 *
 * Some actions, especially showing info messages via DisplayText() are only
 * allowed when the home screen is active, they must not disturb other menus.
 *
 * @return
 * The routine return <i>true</i> when the home screen is active, and
 * <i>false</i> otherwise.
 *
 ******************************************************************************/
bool IsHomeScreen (void)
{
    return (l_MenuIdxStackLevel == 0  &&  l_MenuIdxStack[0] == 0);
}

/***************************************************************************//**
 *
 * @brief	Display Update
 *
 * This function must be called whenever a value which could be displayed on
 * the LCD has been changed.  It calls the currently active display function,
 * which decides what to do, i.e. to update the LC-Display, or not.
 *
 * @param[in] updId
 *	The parameter identifies the value that has been changed.  For a
 *	complete list of values an their identifiers, see @ref UPD_ID.
 *
 * @note
 * 	This function can be called from standard program <b>and</b> from
 * 	interrupt service routines, so it must be designed multithreading
 * 	save!
 *
 ******************************************************************************/
void	DisplayUpdate (UPD_ID updId)
{
DISP_FCT fct;

    /* Parameter check */
    if (updId >= END_UPD_ID)
    {
	EFM_ASSERT(0);		// stall if DEBUG_EFM is set
	return;
    }

    /* Call the currently active Display Function */
    if (l_SimpleMenu)
    {
	fct = (DISP_FCT) l_pMenuCurrList[l_MenuIdxStack[l_MenuIdxStackLevel]];
	fct (updId);
    }
    else
    {
	l_pMenuCurrList[l_MenuIdxStack[l_MenuIdxStackLevel]]->DispFct (updId);
    }
}


/***************************************************************************//**
 *
 * @brief	Display Update for Clock
 *
 * This is a small helper routine to update the display whenever the system
 * time has changed, i.e. every second.  It is usually bound to the AlarmClock
 * module via DisplayUpdateFctInstall().
 *
 ******************************************************************************/
void DisplayUpdateClock (void)
{
    /* call the generic update routine with the identifier UPD_SYSCLOCK */
    DisplayUpdate (UPD_SYSCLOCK);
}


/***************************************************************************//**
 *
 * @brief	Display Update Enable
 *
 * This routine will be called from the DCF77 module when the date and time
 * have been completely received and is valid, so it can be displayed.
 *
 ******************************************************************************/
void	DisplayUpdEnable (void)
{
    if (! l_flgDisplayUpdEnabled)
    {
	l_flgDisplayUpdEnabled = true;	// do this only once after RESET

	/* Time can only be displayed if HOME screen is selected */
	if (IsHomeScreen())
	{
	    /*
	     * Default screen is active, i.e. display date and time, and an
	     * empty line below (or the latest transponder number if there
	     * was received any).  Switch LCD on and trigger display update.
	     */
	    l_flgDisplayOn = true;

	    g_flgIRQ = true;	// keep on running

	    /* Start timer to switch OFF the display after some time */
	    DisplayTimerRestart();
	}
    }
}


/***************************************************************************//**
 *
 * @brief	Switch LCD Off
 *
 * This routine is called from the RTC interrupt handler to trigger the
 * power-off of the LC-Display, after the specified amount of time has elapsed.
 *
 ******************************************************************************/
static void SwitchLCD_Off(TIM_HDL hdl)
{
    (void) hdl;		// suppress compiler warning "unused parameter"

    if (l_flgDisplayUpdEnabled)		// NOT in the very beginning
	l_flgDisplayOn = false;

    g_flgIRQ = true;	// keep on running
}


/***************************************************************************//**
 *
 * @brief	Display Print Formatted
 *
 * This routine is used by Display Modules (DM) to write formatted data to
 * the LC-Display.  This is done in two steps: First a string is build from
 * the given data and stored into a memory buffer.  Later the buffer is written
 * to the LCD.  This function performs the first part and therefore can also
 * be called from interrupts service routines (ISRs).
 *
 * @param[in] lineNum
 *	The line number where to display the text.  Must be 1 or 2.
 *
 * @param[in] frmt
 *	Format string of the text to print - same as for printf().
 *
 * @see
 * 	DisplayText()
 *
 ******************************************************************************/
void	DispPrintf (int lineNum, const char *frmt, ...)
{
va_list		args;


    /* Parameter check */
    if (lineNum < 1  ||  lineNum > 2)
    {
	EFM_ASSERT(false);
	return;
    }

    /* Check if line buffer already contains data not to be overwritten */
    if (Bit(l_flgCopyBufferToLCD, lineNum+1))		// Bit 2 or 3
	return;

    /* Write data to buffer */
    va_start (args, frmt);
    vsprintf (l_LineBuffer[lineNum-1], frmt, args);
    va_end (args);

    /* Set flag for LCD Update */
    Bit(l_flgCopyBufferToLCD, lineNum-1) = 1;

    g_flgIRQ = true;	// keep on running when called from interrupt routine
}


/***************************************************************************//**
 *
 * @brief	Display Text
 *
 * This routine allows you to display text on the LCD.  If the LCD is off,
 * it will be powered-on.  To automatically switch it off after a specified
 * duration, or to display another text after this time, DisplayNext() can
 * be used.
 *
 * @note
 * This routine is only used for writing to the LCD from <b>outside</b> a
 * Display Module (DM), e.g. to notify when an SD-Card has been inserted.
 * This will only happen, when no menu is active, i.e. the default screen
 * is active.<br>
 * In contrast, a DM should use DispPrintf() when writing to the LCD.
 *
 * @param[in] lineNum
 *	The line number where to display the text.  Must be 1 or 2.
 *
 * @param[in] frmt
 *	Format string of the text to print - same as for printf().
 *
 * @see
 * 	DisplayNext(), DispPrintf()
 *
 ******************************************************************************/
void	DisplayText (int lineNum, const char *frmt, ...)
{
va_list		args;


    /* Parameter check */
    if (lineNum < 1  ||  lineNum > 2)
    {
	EFM_ASSERT(false);
	return;
    }

    /* Text can only be displayed if HOME screen is selected */
    if (IsHomeScreen())
    {
	/*
	 * Default screen is active, i.e. display date and time, and an
	 * empty line below (or the latest transponder number if there
	 * was received any).  Switch LCD on and trigger display update.
	 */
	l_flgDisplayOn = true;

	/* Print Text */
	va_start (args, frmt);
	vsprintf (l_LineBuffer[lineNum-1], frmt, args);
	va_end (args);

	/* Set respective flag for LCD Update */
	Bit(l_flgCopyBufferToLCD, lineNum-1) = 1;	// Bit 0 or 1

	/* Also set bit to protect line buffer from being overwritten */
	Bit(l_flgCopyBufferToLCD, lineNum+1) = 1;	// Bit 2 or 3

	g_flgIRQ = true;	// keep on running when called from ISR
    }

    /* Cancel possible running power-off timer to ensure LCD remains ON */
    DisplayTimerCancel();
}


/***************************************************************************//**
 *
 * @brief	Display Next
 *
 * This function defines what should happen next on the LC-Display.  It is
 * typically used after calling DisplayText() in one of the following ways:
 *
 * - @p duration is specified and @p fct is NULL: the LCD is switched off
 *   after the amount of time.
 *
 * - @p duration is specified and @p fct is not NULL: the callback function is
 *   executed after the amount of time.  This function may call DisplayText()
 *   and DisplayNext() again.  In this way it is possible to realize a
 *   "ticker" on the LCD.  For a generic approach a user parameter @p userParm
 *   is passed to the function which can be used to specify the item to be
 *   displayed.
 *
 * - @p duration is 0 and @p fct is NULL: the LCD is switched off immediately.
 *
 * - @p duration is 0 and @p fct is not NULL: the callback function will be
 *   executed as soon as possible.
 *
 * @param[in] duration
 * 	Duration in seconds, <b>after</b> which the specified action should
 * 	occur.  If a duration of 0 is specified, this happens immediately!
 *
 * @param[in] fct
 *	Callback function to be executed after @p duration.  If NULL is
 *	specified, no function will be called, instead the LC-Display is
 *	powered-off.
 *
 * @param[in] userParm
 *	User parameter for function @p fct.
 *
 * @note
 * 	Only one callback function can be installed at a dedicated time, i.e.
 * 	they cannot be stacked.  The function is called in standard context
 * 	by DisplayUpdateCheck() (not by an ISR), so there are no limitations.
 *
 ******************************************************************************/
void	DisplayNext (unsigned int duration, DISP_NEXT_FCT fct, int userParm)
{
    /* Be sure to reset the trigger flag */
    l_DispNextFctTrigger = false;

    /* Cancel possible running timer */
    if (l_hdlDispNext != NONE)
	sTimerCancel (l_hdlDispNext);

    /* Verify if LCD is active */
    if (! l_flgDisplayOn)
	return;			// LCD is already OFF, nothing to display

    /* Store function with argument */
    l_DispNextFct = fct;
    l_DispNextUserParm = userParm;

    /* Start timer for duration, or trigger function immediately */
    if (duration > 0)
    {
	if (l_hdlDispNext != NONE)
	    sTimerStart (l_hdlDispNext, duration);
    }
    else
    {
	l_DispNextFctTrigger = true;
	g_flgIRQ = true;	// keep on running
    }
}


/***************************************************************************//**
 *
 * @brief	Display Next Trigger
 *
 * This routine is called from the RTC interrupt handler to trigger a
 * @ref DISP_NEXT_FCT callback routine, after the specified amount of
 * time is over.  If no callback routine is installed, SwitchLCD_Off()
 * is called instead to switch the LCD off.
 *
 * @see
 * 	DisplayNext(), DisplayText()
 *
 ******************************************************************************/
static void DispNextTrigger(TIM_HDL hdl)
{
    (void) hdl;		// suppress compiler warning "unused parameter"

    /* See if callback routine has been specified */
    if (l_DispNextFct)
    {
	l_DispNextFctTrigger = true;	// set flag to call it from main loop
    }
    else
    {
	/* No callback - switch LCD off if HOME screen is active*/
	if (IsHomeScreen())
	    SwitchLCD_Off((TIM_HDL)0);
    }

    g_flgIRQ = true;	// keep on running
}


/***************************************************************************//**
 *
 * @brief	Copy Buffer to LCD
 *
 * This function writes data from the memory buffers to the LC-Display.
 *
 ******************************************************************************/
static void	CopyBufferToLCD (void)
{
    if (Bit(l_flgCopyBufferToLCD, 0))
    {
	Bit(l_flgCopyBufferToLCD, 0) = 0;
	Bit(l_flgCopyBufferToLCD, 2) = 0;
	LCD_WriteLine(1, l_LineBuffer[0]);
    }

    if (Bit(l_flgCopyBufferToLCD, 1))
    {
	Bit(l_flgCopyBufferToLCD, 1) = 0;
	Bit(l_flgCopyBufferToLCD, 3) = 0;
	LCD_WriteLine(2, l_LineBuffer[1]);
    }
}


/***************************************************************************//**
 *
 * @brief	Generic Menu Handler - leads to further menu entries
 *
 * This generic menu handler leads the user to further menu entries.  It can
 * be used as @ref MENU_FCT in any Display Module when no higher functionality
 * is required.
 *
 * @param[in] keycode
 *	Translated key code, sent by the menu key handler to this module.
 *	This module just returns the keycode to the key	handler.
 *
 * @param[in] arg
 *	Optional argument, not used here.
 *
 * @return
 *	This module does not handle any keys, therefore all are returned to
 *	the menu key handler.
 *
 * @warning
 *	This routine is executed in interrupt context!
 *
 ******************************************************************************/
KEYCODE MenuDistributor (KEYCODE keycode, uint32_t arg)
{
    (void) arg;			// suppress compiler warning "unused parameter"

    /* Nothing to do - forward key codes to the calling routine */

    return keycode;	// let the calling routine handle the key
}
