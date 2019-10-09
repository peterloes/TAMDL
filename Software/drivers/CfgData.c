/***************************************************************************//**
 * @file
 * @brief	Configuration Data
 * @author	Ralf Gerhauser
 * @version	2018-03-25
 *
 * This module reads and parses a configuration file from the SD-Card, and
 * stores the data into a database.  It also provides routines to get access
 * to these parameters.
 *
 ****************************************************************************//*
Revision History:
2019-06-01,rage	- Bugfix in getString: Corrected pointer increment and check
		  for comment or end of line.
2018-03-25,rage	- Added the ability of parsing ENUM definitions.
		- CfgRead BugFix: Open file in read-only mode.
		- Use of drvLEUART_sync() to prevent FIFO overflows.
		- CfgParse: Consider timezone MEZ or MESZ for alarms.
		- Configuring an alarm doesn't need an extra variable any more.
		- Removed unused functionality.
2017-05-02,rage	- Renamed CAM1_DURATION to CAM_DURATION and Cam1Duration to
		  CamDuration.
		- CfgParse: Added new data type CFG_VAR_TYPE_INTEGER.
2017-01-25,rage	Initial version.
*/

/*=============================== Header Files ===============================*/

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "Logging.h"
#include "LEUART.h"
#include "AlarmClock.h"
#include "CfgData.h"
#include "ff.h"		// FS_FAT12/16/32
#include "diskio.h"	// DSTATUS
#include "microsd.h"
#include "DisplayMenu.h"
#include "Control.h"

/*=============================== Definitions ================================*/

    /* local debug: show a list of all IDs and settings */
#define CONFIG_DATA_SHOW	1

/*================================ Local Data ================================*/

    /*! Local pointer to list of configuration variables */
static const CFG_VAR_DEF *l_pCfgVarList;

    /*! Local pointer to list of enum definitions */
static const ENUM_DEF    *l_pEnumDef;

    /*! File handle for log file */
static FIL	l_fh;

    /*! Root pointer to ID list */
static ID_PARM *l_pFirstID;
static ID_PARM *l_pLastID;

    /*! Flag tells if data has been loaded from file */
static bool	l_flgDataLoaded;

/*=========================== Forward Declarations ===========================*/

static void  CfgDataClear (void);
static void  CfgParse (int lineNum, char *line);
static bool  skipSpace (char **ppStr);
static char *getString (char **ppStr);


/***************************************************************************//**
 *
 * @brief	Configuration data initialization
 *
 * This routine must be called once to initialize the configuration data module.
 *
 * @param[in] pCfgVarList
 *	List of configuration variables, the associated data type, and the
 *	address of the respective variable.
 *
 * @param[in] pEnumDef
 *	List of enumerations, each entry contains a NULL-terminated array of
 *	strings with enum names.  The position of a string represents its enum
 *	value, starting with 0 for the first string.  The first element of
 *	<i>pEnumDef</i> is associated with @ref CFG_VAR_TYPE_ENUM_1, the second
 *	entry with @ref CFG_VAR_TYPE_ENUM_2, and so on.
 *
 * @note
 *	Parameter <b>pCfgVarList</b> must point to a persistent data structure,
 *	i.e. this must be valid over the whole life time of the program.
 *
 ******************************************************************************/
void	CfgDataInit (const CFG_VAR_DEF *pCfgVarList, const ENUM_DEF *pEnumDef)
{
    /* Parameter check */
    EFM_ASSERT(pCfgVarList != NULL);

    /* Save configuration */
    l_pCfgVarList = pCfgVarList;
    l_pEnumDef = pEnumDef;
}


/***************************************************************************//**
 *
 * @brief	Read configuration file
 *
 * This routine reads the specified configuration file.
 *
 * @param[in] filename
 *	Name of the configuration file to be read.
 *
 ******************************************************************************/
void	CfgRead (char *filename)
{
FRESULT	 res;		// FatFs function common result code
int	 lineNum;	// current line number
size_t	 i;		// index within line buffer
UINT	 cnt = 0;	// number of bytes read
char	 line[200];	// line buffer (resides on stack)


    /* Switch the SD-Card Interface on */
    MICROSD_PowerOn();

    /* Log reading of the configuration file */
    Log ("Reading Configuration File %s", filename);

    /* Open the file */
    res = f_open (&l_fh, filename,  FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK)
    {
	LogError ("CfgRead: FILE OPEN - Error Code %d", res);
	l_fh.fs = NULL;		// invalidate file handle

	/* Power off the SD-Card Interface */
	MICROSD_PowerOff();
	return;
    }

    /* Discard previous configuration data */
    CfgDataClear();

    /* Assume data can be loaded */
    l_flgDataLoaded = true;

    /* Read configuration file line by line */
    for (lineNum = 1;  ;  lineNum++)
    {
	/* Read line char by char because f_gets() does not check read errors */
	for (i = 0;  i < sizeof(line);  i++)
	{
	    res = f_read(&l_fh, line + i, 1, &cnt);
	    if (res != FR_OK)
	    {
		LogError ("CfgRead: FILE READ - Error Code %d", res);
		l_flgDataLoaded = false;
		break;
	    }
	    if (cnt == 0)
		break;		// end of file detected

	    if (line[i] == '\r')
		i--;		// ignore <CR>
	    else if (line[i] == '\n')
		break;		// read on complete line - process it
	}
	if (res != FR_OK)
	    break;		// abort on error

	/* terminate line buffer with EOS */
	line[i] = EOS;		// substitute <NL> with EOS

	/* Parse line */
	CfgParse (lineNum, line);

	/* Check for end of file */
	if (cnt == 0)
	    break;		// end of file detected

	if (i >= sizeof(line))
	{
	    LogError ("CfgRead: Line %d too long (exceeds %d characters)",
		      lineNum, sizeof(line));
	    break;
	}

	drvLEUART_sync();	// to prevent UART buffer overflow
    }

    /* close file after reading data */
    f_close(&l_fh);

    /* Power off the SD-Card Interface */
    MICROSD_PowerOff();

    /* notify that configuration variables have been changed */
    DisplayUpdate (UPD_CONFIGURATION);

#if CONFIG_DATA_SHOW
    /* show a list of all IDs and settings (will not be logged) */
    CfgDataShow();
#endif
}


/***************************************************************************//**
 *
 * @brief	Clear current configuration data
 *
 * This routine frees all memory which was allocated by the current
 * configuration data.
 *
 ******************************************************************************/
static void  CfgDataClear (void)
{
ID_PARM	*pID, *pID_next;

    if (l_pFirstID)
    {
	/* free memory of ID list */
	for (pID = l_pFirstID;  pID != NULL;  pID = pID_next)
	{
	    pID_next = pID->pNext;

	    free (pID);
	}

	l_pFirstID = l_pLastID = NULL;
    }
}


/***************************************************************************//**
 *
 * @brief	Parse line for variable assignment
 *
 * This routine parses the given line buffer for a variable name and its value.
 * Comments and empty lines will be skipped.
 *
 * @param[in] lineNum
 *	Line number, used to report location in configuration file in case of
 *	an error message is logged.
 *
 * @param[in] line
 *	Line buffer to be parsed.
 *
 ******************************************************************************/
static void CfgParse(int lineNum, char *line)
{
char	*pStr = line;
char	*pStrBegin;
char	 saveChar;
int	 i;
int	 varIdx;
int	 hour, minute;
int32_t	 duration, value;
ID_PARM	 ID_Parm, *pNewID;
ALARM_TIME *pAlarm;
const char **ppEnumName;


    line--;

    if (skipSpace(&pStr))	// skip white space
	return;

    if (*pStr == '#')		// check for comment line
	return;

    /* expect variable name - must start with alpha character */
    if (! isalpha((int)*pStr))
    {
	LogError ("Config File - Line %d, pos %ld: Invalid Variable Name",
		  lineNum, (pStr-line));
	return;
    }

    /* find end of variable name */
    for (pStrBegin = pStr;  isalnum((int)*pStr) || *pStr == '_';  pStr++)
	;

    saveChar = *pStr;		// save character
    *pStr = EOS;		// terminate variable name for compare

    /* search variable in list */
    for (varIdx = 0;  l_pCfgVarList[varIdx].name != NULL;  varIdx++)
	if (strcmp(pStrBegin, l_pCfgVarList[varIdx].name) == 0)
	    break;

    if (l_pCfgVarList[varIdx].name == NULL)
    {
	LogError ("Config File - Line %d, pos %ld: Unknown Variable '%s'",
		  lineNum, (pStrBegin-line), pStrBegin);
	return;
    }

    *pStr = saveChar;		// restore character

    /* equal sign must follow */
    skipSpace(&pStr);		// skip white space

    if (*pStr != '=')
    {
	LogError ("Config File - Line %d, pos %ld: Missing '=' after %s",
		  lineNum, (pStr-line), l_pCfgVarList[varIdx].name);
	return;
    }
    pStr++;

    if (skipSpace(&pStr))	// skip white space
    {
	LogError ("Config File - Line %d, pos %ld: Value expected for %s",
		  lineNum, (pStr-line), l_pCfgVarList[varIdx].name);
	return;
    }

    /* value depends on the type of variable */
    switch (l_pCfgVarList[varIdx].type)
    {
	case CFG_VAR_TYPE_TIME:	// 00:00 to 23:59
	    if (! isdigit((int)*pStr))
	    {
		LogError ("Config File - Line %d, pos %ld, %s: Invalid time",
			  lineNum, (pStr-line), l_pCfgVarList[varIdx].name);
		return;
	    }
	    hour = (int)*(pStr++) - '0';

	    if (isdigit((int)*pStr))
		hour = hour * 10 + (int)*(pStr++) - '0';

	    if (*pStr != ':' || ! isdigit((int)pStr[1]) || ! isdigit((int)pStr[2]))
	    {
		LogError ("Config File - Line %d, pos %ld, %s: Invalid time",
			  lineNum, (pStr-line), l_pCfgVarList[varIdx].name);
		return;
	    }
	    minute = ((int)pStr[1] - '0') * 10 + (int)pStr[2] - '0';
	    pStr += 3;

	    /* all times are given in MEZ - add +1h for MESZ */
	    if (g_isdst)
	    {
		if (++hour > 23)
		    hour = 0;
	    }

	    /* store hours and minutes into variable if one is defined */
	    if (l_pCfgVarList[varIdx].pData != NULL)
	    {
		pAlarm = (ALARM_TIME *)l_pCfgVarList[varIdx].pData;
		pAlarm->Hour   = hour;
		pAlarm->Minute = minute;
	    }

	    /* set alarm time and enable it */
	    AlarmSet (ALARM_UA1_ON_TIME_1 + varIdx, hour, minute);
	    AlarmEnable (ALARM_UA1_ON_TIME_1 + varIdx);
	    break;


	case CFG_VAR_TYPE_DURATION:	// 0 to n seconds
	    for (duration = 0;  isdigit((int)*pStr);  pStr++)
		duration = duration * 10 + (long)*pStr - '0';

	    *((int32_t *)l_pCfgVarList[varIdx].pData) = duration;
	    break;


	case CFG_VAR_TYPE_ID:	// {ID}
	    /* initialize structure */
	    ID_Parm.pNext = NULL;

	    /* get transponder ID */
	    pStrBegin = getString(&pStr);
	    if (pStrBegin == NULL)
		break;			// error

	    /* allocate memory an store ID and parameters */
	    pNewID = malloc(sizeof(ID_Parm) + strlen(pStrBegin) + 1);
	    if (pNewID == NULL)
	    {
		LogError ("Config File - Line %d, pos %ld, %s: OUT OF MEMORY",
			  lineNum, (pStr-line), l_pCfgVarList[varIdx].name);
		return;
	    }

	    *pNewID = ID_Parm;
	    strcpy(pNewID->ID, pStrBegin);

	    if (l_pLastID)
	    {
		l_pLastID->pNext = pNewID;
		l_pLastID = pNewID;
	    }
	    else
	    {
		l_pLastID = l_pFirstID = pNewID;
	    }

	    break;


	case CFG_VAR_TYPE_INTEGER:	// a positive integer variable
	    for (value = 0;  isdigit((int)*pStr);  pStr++)
		value = value * 10 + (long)*pStr - '0';

	    *((uint32_t *)l_pCfgVarList[varIdx].pData) = value;
	    break;


	case CFG_VAR_TYPE_ENUM_1:	// ENUM types
	case CFG_VAR_TYPE_ENUM_2:
	case CFG_VAR_TYPE_ENUM_3:
	case CFG_VAR_TYPE_ENUM_4:
	case CFG_VAR_TYPE_ENUM_5:
	    /* get enum string */
	    pStrBegin = getString(&pStr);
	    if (pStrBegin == NULL)
		break;			// error

	    if (l_pEnumDef == NULL)
	    {
		LogError ("Config File - Line %d, %s: "
			  "No enum names defined", lineNum,
			  l_pCfgVarList[varIdx].name);
		return;
	    }

	    ppEnumName = l_pEnumDef[l_pCfgVarList[varIdx].type
				    - CFG_VAR_TYPE_ENUM_1];

	    for (i =0;  ppEnumName[i] != NULL;  i++)
		if (strcmp (ppEnumName[i], pStrBegin) == 0)
		    break;

	    if (ppEnumName[i] == NULL)
	    {
		LogError ("Config File - Line %d, %s: "
			  "Enum name %s is not valid", lineNum,
			  l_pCfgVarList[varIdx].name, pStrBegin);
		return;
	    }
	    /* Type case (PWR_OUT) is used for ALL types of enums */
	    *((PWR_OUT *)l_pCfgVarList[varIdx].pData) = (PWR_OUT)i;
	    break;


	default:		// unsupported data type
	    LogError ("Config File - Line %d, pos %ld, %s: "
		      "Unsupported data type %d", lineNum, (pStr-line),
		      l_pCfgVarList[varIdx].name, l_pCfgVarList[varIdx].type);
	    return;
    }

    /* check the rest of the line */
    if (skipSpace(&pStr))	// skip white space
	return;

    if (*pStr == '#')		// check for comment line
	return;

    LogError ("Config File - Line %d, pos %ld: Garbage at end of line",
	      lineNum, (pStr-line));
}

// returns true if end of string has been reached
static bool skipSpace (char **ppStr)
{
    /* skip white space */
    while (isspace ((int)**ppStr))
	(*ppStr)++;

    /* return true if End Of String has been reached */
    return (**ppStr == EOS ? true : false);
}

// returns pointer to terminated string, or NULL in case of error
static char *getString (char **ppStr)
{
char	*pStrBegin = *ppStr;

    while (isalnum((int)**ppStr))
	(*ppStr)++;

    /* must be followed by space, comment, or EOS */
    if (**ppStr != EOS)
    {
	*(*ppStr++) = EOS;		// terminate string
        (*ppStr)++;		// continue with next characters

	/* skip optional white space at the end of the string */
	while (isspace ((int)**ppStr))
	    (*ppStr)++;

	/* must be followed by comment or EOS */
	if (**ppStr != '#'  &&  **ppStr != EOS)
	    pStrBegin = NULL;	// return NULL for error
    }

    /* return begin of the string or NULL for error */
    return pStrBegin;
}


/***************************************************************************//**
 *
 * @brief	Lookup transponder ID in configuration data
 *
 * This routine searches the specified transponder ID in the @ref ID_PARM list.
 *
 * @param[in] transponderID
 *	Transponder ID to lookup.
 *
 * @return
 * 	Address of @ref ID_PARM structure of the specified ID, or NULL if the
 * 	ID could not be found.
 *
 ******************************************************************************/
ID_PARM *CfgLookupID (char *transponderID)
{
ID_PARM	*pID;

    for (pID = l_pFirstID;  pID != NULL;  pID = pID->pNext)
	if (strcmp (pID->ID, transponderID) == 0)
	    return pID;

    return NULL;	// ID not found
}


/***************************************************************************//**
 *
 * @brief	Show all configuration data
 *
 * This debug routine shows all configuration data on the serial console.
 *
 ******************************************************************************/
void	 CfgDataShow (void)
{
char	 line[200];
char	*pStr;
int	 i, idx;
int8_t	 hour, minute;
int32_t	 duration;
uint32_t value;
ALARM_TIME *pAlarm;
const char **ppEnumName;
#if 0	// currently no IDs need to be defined
ID_PARM	*pID;
#endif


    drvLEUART_sync();	// to prevent UART buffer overflow

    if (l_pCfgVarList == NULL  ||  ! l_flgDataLoaded)
    {
	drvLEUART_puts ("No Configuration Data loaded\n");
	return;
    }

    /* log all values read from configuration file */
    for (i = 0;  l_pCfgVarList[i].name != NULL;  i++)
    {
	pStr = line;

	if (l_pCfgVarList[i].type == CFG_VAR_TYPE_ID)
	    continue;		// IDs are shown below

	pStr += sprintf (pStr, "%-27s : ", l_pCfgVarList[i].name);

	switch (l_pCfgVarList[i].type)
	{
	    case CFG_VAR_TYPE_TIME:		// 00:00 to 23:59
		if (! AlarmIsEnabled (ALARM_UA1_ON_TIME_1 + i))
		{
		    pStr += sprintf (pStr, "disabled");
		    break;
		}
		pAlarm = (ALARM_TIME *)l_pCfgVarList[i].pData;
		if (pAlarm != NULL)
		{
		    hour   = pAlarm->Hour;
		    minute = pAlarm->Minute;
		}
		else
		{
		    AlarmGet (ALARM_UA1_ON_TIME_1 + i, &hour, &minute);
		}
		pStr += sprintf (pStr, "%02d:%02d", hour, minute);
		break;


	    case CFG_VAR_TYPE_DURATION:	// 0 to n seconds, or A for always
		duration = *((int32_t *)l_pCfgVarList[i].pData);
		if (duration == DUR_INVALID)
		    pStr += sprintf (pStr, "invalid");
		else
		    pStr += sprintf (pStr, "%ld", duration);
		break;


	    case CFG_VAR_TYPE_ID: // {ID}:{CAM_DURATION}:{KEEP_OPEN}:{KEEP_CLOSED}
		/* IDs will be handled separately */
		break;


	    case CFG_VAR_TYPE_INTEGER:	// a positive integer variable
		value = *((uint32_t *)l_pCfgVarList[i].pData);
		pStr += sprintf (pStr, "%lu", value);
		break;

	    case CFG_VAR_TYPE_ENUM_1:	// ENUM types
	    case CFG_VAR_TYPE_ENUM_2:
	    case CFG_VAR_TYPE_ENUM_3:
	    case CFG_VAR_TYPE_ENUM_4:
	    case CFG_VAR_TYPE_ENUM_5:
		if (l_pEnumDef == NULL)
		{
		    pStr += sprintf (pStr, "ERROR: No enum names defined");
		    break;
		}
		idx = *((PWR_OUT *)l_pCfgVarList[i].pData);
		if (idx < 0)
		{
		    pStr += sprintf (pStr, "not set");
		    break;
		}
		ppEnumName = l_pEnumDef[l_pCfgVarList[i].type
					- CFG_VAR_TYPE_ENUM_1];
		pStr += sprintf (pStr, "%s", ppEnumName[idx]);
		break;

	    default:		// unsupported data type
		LogError ("l_pCfgVarList[%d], %s: Unsupported data type %d",
			  i, l_pCfgVarList[i].name, l_pCfgVarList[i].type);
		break;
	}

	sprintf (pStr, "\n");
	drvLEUART_puts (line);
	drvLEUART_sync();	// to prevent UART buffer overflow
    }

#if 0	// currently no IDs need to be defined
    /* print list of IDs */
    if (l_pFirstID == NULL)
    {
	drvLEUART_puts("Warning: No IDs have been defined in configuration file\n");
    }
    else
    {
	for (pID = l_pFirstID;  pID != NULL;  pID = pID->pNext)
	{
	    sprintf (line, "%-20s\n", pID->ID);
	    drvLEUART_puts (line);
	    drvLEUART_sync();	// to prevent UART buffer overflow
	}
    }
#endif
}
