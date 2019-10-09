/***************************************************************************//**
 * @file
 * @brief	Header file for LCD_DOGM162.c
 * @author	Ralf Gerhauser
 * @version	2018-03-02
 *
 * This is the header file of module "LCD_DOGM162.c"
 *
 ****************************************************************************//*
Revision History:
2018-03-02,rage	Removed definition for "fields" because "lines" are enough.
		Added custom characters LCD_ARROW_UP/DOWN/RIGHT/LEFT.
		Added prototype for LCD_WriteLine().
2014-11-19,rage	Initial version.
*/
#ifndef __INC_LCD_DOGM162_h
#define __INC_LCD_DOGM162_h

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================== Definitions ================================*/

    /*!@name LCD Dimensions. */
//@{
#define LCD_DIMENSION_X	16	//!< X dimension is 16 characters.
#define LCD_DIMENSION_Y  2	//!< Y dimension is 2 lines.
//@}

    /*!@name Special Characters. */
//@{
#define LCD_ARROW_UP	"\05"	//!< Up Arrow
#define LCD_ARROW_DOWN	"\06"	//!< Down Arrow
#define LCD_ARROW_RIGHT	"\07"	//!< Right Arrow
#define LCD_ARROW_LEFT	"\010"	//!< Left Arrow
//@}

/*================================ Prototypes ================================*/

    /* Regular functions */
void LCD_Init(void);
void LCD_PowerOn(void);
void LCD_PowerOff(void);
void LCD_Printf (int lineNum, const char *frmt, ...);
void LCD_vPrintf(int lineNum, const char *frmt, va_list args);
void LCD_WriteLine (int lineNum, char *buffer);
void LCD_Puts (char *pStr);
void LCD_Putc (char c);
void LCD_GotoXY (uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif


#endif /* __INC_LCD_DOGM162_h */
