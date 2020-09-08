/***************************************************************************//**
 * @file
 * @brief	Header file of module CfgData.c
 * @author	Ralf Gerhauser
 * @version	2018-03-25
 ****************************************************************************//*
Revision History:
2018-03-25,rage	- Added ENUM_DEF to be able to use ENUM definitions.
		- Defined CFG_VAR_TYPE_ENUM_1 to 5.
		- Changed prototype for CfgDataInit().
2017-05-02,rage	- Renamed Cam1Duration to CamDuration.
		- Added data types CFG_VAR_TYPE_INTEGER and CFG_VAR_TYPE_CONFIG.
2017-01-25,rage	Initial version.
*/

#ifndef __INC_CfgData_h
#define __INC_CfgData_h

/*=============================== Header Files ===============================*/

#include "config.h"		// include project configuration parameters

/*=============================== Definitions ================================*/

    /*!@brief Configuration variable data types. */
typedef enum
{
    CFG_VAR_TYPE_TIME,		//!< 00:00 to 23:59
    CFG_VAR_TYPE_DURATION,	//!< 0 to n seconds
    CFG_VAR_TYPE_ID,		//!< transponder ID
    CFG_VAR_TYPE_INTEGER,	//!< positive integer variable (0 to n)
    CFG_VAR_TYPE_CONFIG,	//!< configuration data
    CFG_VAR_TYPE_ENUM_1,	//!< enumeration 1
    CFG_VAR_TYPE_ENUM_2,	//!< enumeration 2
    CFG_VAR_TYPE_ENUM_3,	//!< enumeration 3
    CFG_VAR_TYPE_ENUM_4,	//!< enumeration 4
    CFG_VAR_TYPE_ENUM_5,	//!< enumeration 5
    END_CFG_VAR_TYPE
} CFG_VAR_TYPE;

    /*!@brief Special states for @ref CFG_VAR_TYPE_DURATION. */
#define DUR_INVALID (-1)	//!< entry is invalid

    /*!@brief Structure to define configuration variables. */
typedef struct
{
    const char	      *name;	//!< variable name
    const CFG_VAR_TYPE type;	//!< variable type
    const void	      *pData;	//!< address of data variable
} CFG_VAR_DEF;

    /*!@brief Type for a list of enum definitions. */
typedef const char **ENUM_DEF;	//!< list of enum names

    /*!@brief Individual Parameters for a specific transponder ID. */
typedef struct _ID_PARM
{
    struct _ID_PARM *pNext;	//!< pointer to next ID, or NULL
#if 0	//RAGE: Currently there are no parameters defined for a Transponder ID
    int32_t  KeepOpen;		// individual KEEP_OPEN duration
    int32_t  KeepClosed;	// individual KEEP_CLOSED duration
    int32_t  CamDuration;	// individual CAM_DURATION duration
#endif
    char     ID[];		//!< ID string follows
} ID_PARM;


/*================================ Prototypes ================================*/

    /* Initialize the configuration data module */
void	 CfgDataInit (const CFG_VAR_DEF *pCfgVarList, const ENUM_DEF *pEnumDef);

    /* Read configuration file */
void	 CfgRead	(char *filename);

    /* Lookup transponder ID in database */
ID_PARM *CfgLookupID	(char *transponderID);

    /* Show all configuration data */
void	 CfgDataShow (void);


#endif /* __INC_CfgData_h */
