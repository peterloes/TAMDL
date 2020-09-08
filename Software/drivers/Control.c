/***************************************************************************//**
 * @file
 * @brief	Sequence Control
 * @author	Ralf Gerhauser
 * @version	2020-05-12
 *
 * This is the automatic sequence control module.  It controls the power
 * outputs and the measurement of their voltage and current.  Calibration
 * routines and the ability to write data to an @ref EEPROM area makes it
 * possible to store board-specific factors, so, voltage and current can be
 * calculated and logged .<br>
 * This module also defines the configuration variables for the file
 * <a href="../../CONFIG.TXT"><i>CONFIG.TXT</i></a>.
 *
 ****************************************************************************//*
Revision History:
2020-05-12,rage	- Use defines XXX_POWER_ALARM instead of ENUMs.
		- Power Alarms are grouped in ON and OFF alarms now.
		- Changed AlarmPowerControl() according to new ALARM_ID enums.
2019-02-09,rage	- Added configuration variable SCAN_DURATION which specifies the
		  measurement duration of a single channel in [ms].  Valid range
		  is 52ms to 2200ms.  There are four channels: U1, I1, U2, I2.
2018-10-10,rage	Implemented Power Cycling for each output during the on-times.
2018-03-26,rage	Initial version, based on MAPRDL.
*/

/*=============================== Header Files ===============================*/

#include "em_cmu.h"
#include "em_int.h"
#include "em_gpio.h"
#include "em_adc.h"
#include "eeprom_emulation.h"
#include "ExtInt.h"
#include "Logging.h"
#include "AlarmClock.h"
#include "RFID.h"
#include "CfgData.h"
#include "Control.h"
#include "PowerFail.h"
#include "DisplayMenu.h"
#include "BatteryMon.h"
#include "DM_PowerOutput.h"	// g_UA_Calib_mV[] and g_UA_Calib_mA[]

/*=============================== Definitions ================================*/

/*!@brief Magic ID for EEPROM (Flash) block */
#define MAGIC_ID	0x0815

    /*!@brief Power output measuring appliances. */
typedef enum
{
    MEASURE_NONE = NONE,	// (-1) for no measuring at all
    MEASURE_UA1,		// 0: measure U/I for UA1 (X2-5)
    MEASURE_UA2,		// 1: measure U/I for UA2 (X2-7)
    NUM_MEASURE
} MEASURE;

    /*!@brief Structure to handle measuring. */
typedef struct
{
    __IO uint32_t *BitBandAddr;	//!< Bit band address of measuring enable pin
    TIM_HDL    hdlFollowUpTime;	//!< Timer handle for follow-up time
    uint32_t   FollowUpTime;	//!< Time in [s] while measuring is still done
				//!< after the corresponding output is switched off
    ADC_SingleInput_TypeDef ChanU;  //!< ADC Channel to measure voltage U
    uint32_t   U_MinDiff;	//!< minimum difference for a new U-value [mV]
    ADC_SingleInput_TypeDef ChanI;  //!< ADC Channel to measure current I
    uint32_t   I_MinDiff;	//!< minimum difference for a new I-value [mA]
} MEASURE_DEF;

    /*!@brief Structure to define power outputs. */
typedef struct
{
    __IO uint32_t *BitBandAddr;	// Bit band address of GPIO power enable pin
    MEASURE        Measure;	// Enum for U/I measuring
} PWR_OUT_DEF;

    /*!@brief Macro to calculate a GPIO bit address for a port and pin. */
#define GPIO_BIT_ADDR(port, pin)					\
	IO_BIT_ADDR((&(GPIO->P[(port)].DOUT)), (pin))

/*!@brief Macro to extract the port number from a GPIO bit address.  */
#define GPIO_BIT_ADDR_TO_PORT(bitAddr)		(GPIO_Port_TypeDef)	\
	(((((uint32_t)(bitAddr) - BITBAND_PER_BASE) >> 5)		\
	 + PER_MEM_BASE - GPIO_BASE) / sizeof(GPIO_P_TypeDef))

    /*!@brief Macro to extract the pin number from a GPIO bit address.  */
#define GPIO_BIT_ADDR_TO_PIN(bitAddr)					\
	(((uint32_t)(bitAddr) >> 2) & 0x1F)

/*================================ Global Data ===============================*/

    /*!@brief CFG_VAR_TYPE_ENUM_2: Enum names for Power Outputs. */
const char *g_enum_PowerOutput[] = { "UA1", "UA2", "BATT", NULL };

    /*!@brief Power Cycle Interval for UA1, UA2, and BATT in [s], 0=disable */
int32_t		g_PwrInterval[NUM_PWR_OUT];

    /*!@brief Power Cycle ON duration for UA1, UA2, and BATT in [s], 0=disable */
int32_t		g_On_Duration[NUM_PWR_OUT];

/*================================ Local Data ================================*/

    /*!@brief Timer handles for UA1, UA2, and BATT Power Cycle Interval */
static TIM_HDL		l_hdlPwrInterval[NUM_PWR_OUT] = { NONE, NONE, NONE };

    /*!@brief Power output port and pin assignment, see @ref PWR_OUT. */
static const PWR_OUT_DEF  l_PwrOutDef[NUM_PWR_OUT] =
{   //     BitBandAddr,                 Measure
    { GPIO_BIT_ADDR(gpioPortA, 3), MEASURE_UA1  }, // PA3 enables PWR_OUT_UA1
    { GPIO_BIT_ADDR(gpioPortA, 4), MEASURE_UA2  }, // PA4 enables PWR_OUT_UA2
    { GPIO_BIT_ADDR(gpioPortA, 6), MEASURE_NONE }, // PA6 enables PWR_OUT_BATT
};

    /*!@brief Scan duration in [ms] for one of four channels (U1,I1,U2,I2). */
static uint32_t     l_ScanDuration = DFLT_SCAN_DURATION;
#define MIN_SCAN_DURATION	  52	// minimum is   52ms
#define MAX_SCAN_DURATION	2200	// maximum is 2200ms

    /*!@brief Definitions for measurements, see @ref MEASURE. */
static MEASURE_DEF  l_MeasureDef[NUM_MEASURE] =
{   // BitBandAddr,
    // hdlFollowUpTime,   FollowUpTime,
    // ChanU,             U_MinDiff,
    // ChanI,             I_MinDiff
// [MEASURE_UA1]
    { GPIO_BIT_ADDR(gpioPortC, 8),		// PC8 enables measuring UA1
      NONE,  DFLT_MEASURE_FOLLOW_UP_TIME,	// timer handle and FollowUpTime
      adcSingleInpCh6, DFLT_MEASURE_U_MIN_DIFF,	// ADC U-Channel, min. diff [mV]
      adcSingleInpCh0, DFLT_MEASURE_I_MIN_DIFF	// ADC I-Channel, min. diff [mA]
    },
// [MEASURE_UA2]
    { GPIO_BIT_ADDR(gpioPortC, 9),		// PC9 enables measuring UA2
      NONE,  DFLT_MEASURE_FOLLOW_UP_TIME,	// timer handle and FollowUpTime
      adcSingleInpCh7, DFLT_MEASURE_U_MIN_DIFF,	// ADC U-Channel, min. diff [mV]
      adcSingleInpCh3, DFLT_MEASURE_I_MIN_DIFF	// ADC I-Channel, min. diff [mA]
    },
};

    /*!@brief Bit mask of all ADC channels that should be scanned. */
static uint8_t		l_ADC_ScanChanMask;

    /*!@brief Bit mask of active (enabled) ADC channels. */
static uint8_t		l_ADC_ActiveChanMask;

    /*!@brief Array holds indexes for data storage, see @ref l_ADC_Value. */
static uint8_t		l_ADC_ChanIdxMap[8];	// 8 external ADC Channels

    /*!@brief Array holds the raw values of the ADC channels that have been
     * scanned.  UA1: [0]=voltage [1]=current,  UA2: [2]=voltage [3]=current
     */
static uint32_t		l_ADC_Value[NUM_MEASURE * 2];

    /*!@brief Previous voltage and current values */
static uint32_t		l_prev_value_mV[NUM_MEASURE];
static int		l_prev_BATT_mV;
static uint32_t		l_prev_value_mA[NUM_MEASURE];
static int		l_prev_BATT_mA;

    /*!@brief Follow-Up Time for measuring BATT_INP */
static uint32_t		l_BATT_FollowUpTime;

    /*!@brief Timer handle for BATT_INP Follow-Up Time */
static TIM_HDL		l_hdlFollowUpTimeBATT = NONE;

    /*!@brief Flag to enable BATT_INP measuring */
static bool		l_flgLogBATT;

    /*!@brief Minimum time between two successive BATT_INP Measurements [ms] */
static uint32_t		l_BATT_MeasureInterval;

    /*!@brief Minimum difference for a new Battery U-value [mV] */
static uint32_t		l_BATT_U_MinDiff;
    /*!@brief Minimum difference for a new Battery I-value [mA] */
static uint32_t		l_BATT_I_MinDiff;

    /*!@brief Bit mask of ADC values that have been updated, see @ref l_ADC_Value. */
static volatile uint8_t	l_ADC_ValueUpdateMask;

    /*!@brief Flag if ADC should be switched on. */
static volatile bool	l_flgADC_On;		// is false for default

    /*!@brief Current state of the ADC: true means ON, false means OFF. */
static volatile bool	l_flgADC_IsOn;		// is false for default

    /*!@brief Define the non-volatile variables. */
static EE_Variable_TypeDef  magic, ua1mV_h, ua1mV_l, ua1mA_h, ua1mA_l,
				   ua2mV_h, ua2mV_l, ua2mA_h, ua2mA_l, chksum;

    /*!@brief Dividers for calculating [mV] values of UA1, UA2. */
static volatile uint32_t l_mV_Divider[NUM_MEASURE];

    /*!@brief Dividers for calculating [mA] values of UA1, UA2. */
static volatile uint32_t l_mA_Divider[NUM_MEASURE];

    /*!@brief List of configuration variables.
     * Alarm times, i.e. @ref CFG_VAR_TYPE_TIME must be defined first, because
     * the array index is used to specify the alarm number \<alarmNum\>,
     * starting with @ref UA1_ON_TIME_1, when calling AlarmSet().
     * Keep sequence in sync with @ref ALARM_ID!
     */
static const CFG_VAR_DEF l_CfgVarList[] =
{
    // Alarm times (must be consecutive)
    { "UA1_ON_TIME_1",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_ON_TIME_2",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_ON_TIME_3",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_ON_TIME_4",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_ON_TIME_5",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_ON_TIME_1",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_ON_TIME_2",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_ON_TIME_3",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_ON_TIME_4",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_ON_TIME_5",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_ON_TIME_1",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_ON_TIME_2",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_ON_TIME_3",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_ON_TIME_4",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_ON_TIME_5",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_OFF_TIME_1",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_OFF_TIME_2",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_OFF_TIME_3",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_OFF_TIME_4",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA1_OFF_TIME_5",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_OFF_TIME_1",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_OFF_TIME_2",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_OFF_TIME_3",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_OFF_TIME_4",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "UA2_OFF_TIME_5",		CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_OFF_TIME_1",	CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_OFF_TIME_2",	CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_OFF_TIME_3",	CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_OFF_TIME_4",	CFG_VAR_TYPE_TIME,	NULL		      },
    { "BATT_OFF_TIME_5",	CFG_VAR_TYPE_TIME,	NULL		      },
    // Power Cycling Intervals for UA1, UA2, and BATT
    { "UA1_INTERVAL",	 CFG_VAR_TYPE_DURATION, &g_PwrInterval[PWR_OUT_UA1]   },
    { "UA1_ON_DURATION", CFG_VAR_TYPE_DURATION, &g_On_Duration[PWR_OUT_UA1]   },
    { "UA2_INTERVAL",	 CFG_VAR_TYPE_DURATION, &g_PwrInterval[PWR_OUT_UA2]   },
    { "UA2_ON_DURATION", CFG_VAR_TYPE_DURATION, &g_On_Duration[PWR_OUT_UA2]   },
    { "BATT_INTERVAL",	 CFG_VAR_TYPE_DURATION, &g_PwrInterval[PWR_OUT_BATT]  },
    { "BATT_ON_DURATION",CFG_VAR_TYPE_DURATION, &g_On_Duration[PWR_OUT_BATT]  },
    // Configuration of the RFID Reader
    { "RFID_TYPE",		CFG_VAR_TYPE_ENUM_1,	&g_RFID_Type          },
    { "RFID_POWER",		CFG_VAR_TYPE_ENUM_2,	&g_RFID_Power         },
    { "RFID_ABSENT_DETECT_TIMEOUT",CFG_VAR_TYPE_INTEGER,
					&g_RFID_AbsentDetectTimeout	      },
    // Measuring configuration
    { "SCAN_DURATION", CFG_VAR_TYPE_INTEGER,  &l_ScanDuration		      },
    { "UA1_MEASURE_FOLLOW_UP_TIME", CFG_VAR_TYPE_INTEGER,
					&l_MeasureDef[0].FollowUpTime	      },
    { "UA1_MEASURE_U_MIN_DIFF",	CFG_VAR_TYPE_INTEGER,
					&l_MeasureDef[0].U_MinDiff	      },
    { "UA1_MEASURE_I_MIN_DIFF",	CFG_VAR_TYPE_INTEGER,
					&l_MeasureDef[0].I_MinDiff	      },
    { "UA2_MEASURE_FOLLOW_UP_TIME", CFG_VAR_TYPE_INTEGER,
					&l_MeasureDef[1].FollowUpTime	      },
    { "UA2_MEASURE_U_MIN_DIFF",	CFG_VAR_TYPE_INTEGER,
					&l_MeasureDef[1].U_MinDiff	      },
    { "UA2_MEASURE_I_MIN_DIFF",	CFG_VAR_TYPE_INTEGER,
					&l_MeasureDef[1].I_MinDiff	      },
    { "BATT_MEASURE_FOLLOW_UP_TIME",CFG_VAR_TYPE_INTEGER,&l_BATT_FollowUpTime },
    { "BATT_MEASURE_U_MIN_DIFF",CFG_VAR_TYPE_INTEGER,	&l_BATT_U_MinDiff     },
    { "BATT_MEASURE_I_MIN_DIFF",CFG_VAR_TYPE_INTEGER,	&l_BATT_I_MinDiff     },
    { "UA1_CALIBRATE_mV",	CFG_VAR_TYPE_INTEGER,	&g_UA_Calib_mV[0]     },
    { "UA1_CALIBRATE_mA",	CFG_VAR_TYPE_INTEGER,	&g_UA_Calib_mA[0]     },
    { "UA2_CALIBRATE_mV",	CFG_VAR_TYPE_INTEGER,	&g_UA_Calib_mV[1]     },
    { "UA2_CALIBRATE_mA",	CFG_VAR_TYPE_INTEGER,	&g_UA_Calib_mA[1]     },
    {  NULL,			END_CFG_VAR_TYPE,	NULL		      }
};

    /*!@brief List of all enum definitions. */
static const ENUM_DEF l_EnumList[] =
{
    g_enum_RFID_Type,		// CFG_VAR_TYPE_ENUM_1
    g_enum_PowerOutput,		// CFG_VAR_TYPE_ENUM_2
};

uint32_t	l_dbg_ADC_IF;		// debug error condition of ADC
static uint32_t l_dbg_ADC_ErrCnt;	// debug error count for ADC
static uint32_t l_dbg_ADC_NotReadyCnt;
static uint32_t l_dbg_ADC_OvflErrCnt[4];
static uint32_t l_dbg_ADC_ChanCnt[4];	// count normal conversions per channel


/*=========================== Forward Declarations ===========================*/

static void	AlarmPowerControl (int alarmNum);
static void	IntervalPowerControl (TIM_HDL hdl);
static void	MeasureStop (TIM_HDL hdl);
static void	MeasureStopBATT (TIM_HDL hdl);
static void	ADC_ScanStart (void);
static void	ADC_ScanStop (void);
static void	ReadCalibrationData(void);


/***************************************************************************//**
 *
 * @brief	Initialize control module
 *
 * This routine initializes the sequence control module.
 *
 ******************************************************************************/
void	ControlInit (void)
{
int	i;
int	m;
int	chan;

    /* Enables the flash controller for writing. */
    MSC_Init();

    /* Initialize the eeprom emulator using 3 pages. */
    if ( !EE_Init(DEFAULT_NUMBER_OF_PAGES) )
    {
	/* If the initialization fails we have to take some measure
	 * to obtain a valid set of pages. In this example we simply
	 * format the pages
	 */
	EE_Format(DEFAULT_NUMBER_OF_PAGES);
    }

    /* Declare variables (virtual addresses) */
    EE_DeclareVariable(&magic);
    EE_DeclareVariable(&ua1mV_h);
    EE_DeclareVariable(&ua1mV_l);
    EE_DeclareVariable(&ua1mA_h);
    EE_DeclareVariable(&ua1mA_l);
    EE_DeclareVariable(&ua2mV_h);
    EE_DeclareVariable(&ua2mV_l);
    EE_DeclareVariable(&ua2mA_h);
    EE_DeclareVariable(&ua2mA_l);
    EE_DeclareVariable(&chksum);

    /* Read dividers for UA1/UA2 voltage and current calculation from flash */
    ReadCalibrationData();

    /* Introduce variable list to configuration data module */
    CfgDataInit (l_CfgVarList, l_EnumList);

    /* Create required timers */
    for (m = 0;  m < NUM_MEASURE;  m++)
    {
	if (l_MeasureDef[m].hdlFollowUpTime == NONE)
	    l_MeasureDef[m].hdlFollowUpTime = sTimerCreate (MeasureStop);
    }

    if (l_hdlFollowUpTimeBATT == NONE)
	l_hdlFollowUpTimeBATT = sTimerCreate (MeasureStopBATT);

    for (i = 0;  i < NUM_PWR_OUT;  i++)
    {
	if (l_hdlPwrInterval[i] == NONE)
	    l_hdlPwrInterval[i] = sTimerCreate (IntervalPowerControl);
    }

    /* Initialize power output enable pins */
    for (i = 0;  i < NUM_PWR_OUT;  i++)
    {
	/* Configure Power Enable Pin, switch it OFF per default */
	GPIO_PinModeSet (GPIO_BIT_ADDR_TO_PORT(l_PwrOutDef[i].BitBandAddr),
			 GPIO_BIT_ADDR_TO_PIN (l_PwrOutDef[i].BitBandAddr),
			 gpioModePushPull, 0);

	/* Check if there exists an associated measuring facility */
	m = l_PwrOutDef[i].Measure;
	if (m != MEASURE_NONE)
	{
	    /* Configure associated Enable Pin for U/I Measuring */
	    GPIO_PinModeSet (GPIO_BIT_ADDR_TO_PORT(l_MeasureDef[m].BitBandAddr),
			     GPIO_BIT_ADDR_TO_PIN (l_MeasureDef[m].BitBandAddr),
			     gpioModePushPull, 0);

	    /* Add channels to ADC scan, and setup index for data storage */
	    chan = l_MeasureDef[m].ChanU;
	    Bit(l_ADC_ScanChanMask, chan) = 1;
	    l_ADC_ChanIdxMap[chan] = (m * 2);		// idx for l_ADC_Value[]

	    chan = l_MeasureDef[m].ChanI;
	    Bit(l_ADC_ScanChanMask, chan) = 1;
	    l_ADC_ChanIdxMap[chan] = (m * 2) + 1;	// idx for l_ADC_Value[]
	}
    }

    /* Use same routine for all power-related alarms */
    for (i = FIRST_POWER_ALARM;  i <= LAST_POWER_ALARM;  i++)
	AlarmAction (i, AlarmPowerControl);

    /* Initialize configuration with default values */
    ClearConfiguration();
}


/***************************************************************************//**
 *
 * @brief	Clear Configuration
 *
 * This routine sets all configuration variables to default values.  It must be
 * executed <b>before</b> calling CfgRead() to ensure the correct settings
 * for variables which are <b>not</b> set within a new configuration.
 *
 ******************************************************************************/
void	ClearConfiguration (void)
{
int	i;

    /* Disable all power-related alarms */
    for (i = FIRST_POWER_ALARM;  i <= LAST_POWER_ALARM;  i++)
	AlarmDisable(i);

    /* Disable Power Cycle Interval */
    for (i = 0;  i < NUM_PWR_OUT;  i++)
    {
	g_PwrInterval[i] = 0;
	g_On_Duration[i] = 0;
    }

    /* Disable RFID functionality */
    g_RFID_Type = RFID_TYPE_NONE;
    g_RFID_Power = PWR_OUT_NONE;
    g_RFID_AbsentDetectTimeout = DFLT_RFID_ABSENT_DETECT_TIMEOUT;

    /* Set measurements values to defaults */
    l_ScanDuration = DFLT_SCAN_DURATION;
    for (i = 0;  i < 2;  i++)
    {
	l_MeasureDef[i].FollowUpTime = DFLT_MEASURE_FOLLOW_UP_TIME;
	l_MeasureDef[i].U_MinDiff = DFLT_MEASURE_U_MIN_DIFF;
	l_MeasureDef[i].I_MinDiff = DFLT_MEASURE_I_MIN_DIFF;
    }

    l_BATT_FollowUpTime = DFLT_MEASURE_FOLLOW_UP_TIME;	// for BATT_INP
    l_BATT_U_MinDiff = DFLT_MEASURE_U_MIN_DIFF;
    l_BATT_I_MinDiff = DFLT_MEASURE_I_MIN_DIFF;

    /* Clear Calibration reference values */
    for (i = 0;  i < 2;  i++)
	g_UA_Calib_mV[i] = g_UA_Calib_mA[i] = 0;
}


/***************************************************************************//**
 *
 * @brief	Verify Configuration
 *
 * This routine verifies if the new configuration values are valid.  It must
 * be executed <b>after</b> calling CfgRead().
 *
 ******************************************************************************/
void	VerifyConfiguration (void)
{
#define MIN_VAL_INTERVAL	10	//<! Minimum Power Cycle Interval in [s]
#define MIN_VAL_ON_DURATION	 5	//<! Minimum ON Duration in [s]
#define MIN_VAL_OFF_DURATION	 5	//<! Minimum OFF Duration in [s]
int	i;
bool	error;
int32_t	interval, duration;

    /* Verify Power Cycle Interval */
    for (i = 0;  i < NUM_PWR_OUT;  i++)
    {
	error = false;
	interval = g_PwrInterval[i];
	duration = g_On_Duration[i];

	if (interval <= 0)
	    continue;			// this channel is inactive

	if (interval < MIN_VAL_INTERVAL)
	{
	    LogError ("Config File - %s_INTERVAL: Value %ds is too small,"
		      " minimum is %ds",
		      g_enum_PowerOutput[i], interval, MIN_VAL_INTERVAL);
	    error = true;
	}
	else if (duration < MIN_VAL_ON_DURATION)
	{
	    LogError ("Config File - %s_ON_DURATION: Value %ds is too small,"
		      " minimum is %ds",
		      g_enum_PowerOutput[i], duration, MIN_VAL_ON_DURATION);
	    error = true;
	}
	else if (interval - duration < MIN_VAL_OFF_DURATION)
	{
	    LogError ("Config File - %s_ON_DURATION: Off duration of %ds is"
		      " too small, limit the On duration!",
		      g_enum_PowerOutput[i], interval - duration);
	    error = true;
	}

	if (error)
	{
	    g_PwrInterval[i] = (-1);
	    g_On_Duration[i] = (-1);
	}
    }

    /* Verify Scan Time */
    if (l_ScanDuration < MIN_SCAN_DURATION)
    {
	LogError ("Config File - SCAN_DURATION: Scan time of %ldms is too small,"
		  " limiting it to %dms", l_ScanDuration, MIN_SCAN_DURATION);
	l_ScanDuration = MIN_SCAN_DURATION;
    }
    else if (l_ScanDuration > MAX_SCAN_DURATION)
    {
	LogError ("Config File - SCAN_DURATION: Scan time of %ldms is too long,"
		  " limiting it to %dms", l_ScanDuration, MAX_SCAN_DURATION);
	l_ScanDuration = MAX_SCAN_DURATION;
    }
}


/***************************************************************************//**
 *
 * @brief	Control
 *
 * This routine is periodically called by the main execution loop to perform
 * miscellaneous control tasks, especially measurement activation.
 *
 ******************************************************************************/
void	Control (void)
{
int	m;
uint32_t value_mV, diff_mV;
uint32_t value_mA, diff_mA;
int	 batt_mV, batt_mA;
bool	flgLogUA;
static bool	flgLogBATT = false;
static uint32_t	delayStart;
#define	MEASUREMENT_INTERVAL	500 // ms


    /* ADC control */
    if (l_flgADC_On)
    {
	/* ADC should be switched ON */
	if (! l_flgADC_IsOn)
	{
#ifdef LOGGING
	    /* Generate Log Message */
	    Log ("ADC is switched ON");
#endif
	    /* Start ADC Scan */
	    ADC_ScanStart();

	    l_flgADC_IsOn = true;

	    /* initialize delay values */
	    delayStart = msDelayStart();
	    l_BATT_MeasureInterval = 0;		// this time: NO delay
	}
    }
    else
    {
	/* ADC should be switched OFF */
	if (l_flgADC_IsOn)
	{
	    /* Stop ADC */
	    ADC_ScanStop();

	    l_flgADC_IsOn = false;

#ifdef LOGGING
	    /* Generate Log Message */
	    Log ("ADC is switched off");
#endif
	}
    }

    /* Measurement of the Power Outputs */
    for (m = 0;  m < NUM_MEASURE;  m++)
    {
	/* Check if measurement of this channel is active */
	if (Bit(l_ADC_ActiveChanMask, l_MeasureDef[m].ChanU))
	{
	    value_mV = PowerVoltage(PWR_OUT_UA1 + (PWR_OUT)m) + 50;  // round
	    value_mA = PowerCurrent(PWR_OUT_UA1 + (PWR_OUT)m);

	    if (value_mV > l_prev_value_mV[m])
		diff_mV = value_mV - l_prev_value_mV[m];
	    else
		diff_mV = l_prev_value_mV[m] - value_mV;

	    if (value_mA > l_prev_value_mA[m])
		diff_mA = value_mA - l_prev_value_mA[m];
	    else
		diff_mA = l_prev_value_mA[m] - value_mA;

	    flgLogUA = false;

	    if (diff_mV >= l_MeasureDef[m].U_MinDiff)
	    {
		l_prev_value_mV[m] = value_mV;
		flgLogUA = true;
	    }

	    if (diff_mA >= l_MeasureDef[m].I_MinDiff)
	    {
		l_prev_value_mA[m] = value_mA;
		flgLogUA = true;
	    }

	    if (flgLogUA)
	    {
		Log ("UA%d     : %2ld.%ldV %4ldmA", m + 1,
		     (value_mV / 1000), (value_mV % 1000) / 100,
		     value_mA);
		flgLogBATT =  true;	// also log Battery input data
	    }
	}
    }

    /*
     * Log data from the Battery controller if required.  Unfortunately the
     * firmware of the battery controller seems to be quite buggy, so we need
     * to pause between SMBus accesses.
     */
    if ((flgLogBATT  ||  l_flgLogBATT)
    &&  msDelayIsDone(delayStart, l_BATT_MeasureInterval))
    {
	/* initialize delay values */
	delayStart = msDelayStart();
	l_BATT_MeasureInterval = MEASUREMENT_INTERVAL;

	/* get data from battery controller */
	batt_mV = BatteryRegReadWord (SBS_Voltage);
	msDelay(100);		// to prevent hang-up of battery controller
	batt_mA = BatteryRegReadWord (SBS_BatteryCurrent);

	if (! flgLogBATT)	// only Battery measurement is active
	{
	    /* calculate differences to previous measurement */
	    if (batt_mV > l_prev_BATT_mV)
		diff_mV = batt_mV - l_prev_BATT_mV;
	    else
		diff_mV = l_prev_BATT_mV - batt_mV;

	    if (batt_mA > l_prev_BATT_mA)
		diff_mA = batt_mA - l_prev_BATT_mA;
	    else
		diff_mA = l_prev_BATT_mA - batt_mA;

	    if (diff_mV >= l_BATT_U_MinDiff)
	    {
		l_prev_BATT_mV = batt_mV;
		flgLogBATT = true;
	    }

	    if (diff_mA >= l_BATT_I_MinDiff)
	    {
		l_prev_BATT_mA = batt_mA;
		flgLogBATT = true;
	    }
	}

	if (flgLogBATT)
	{
	    if (batt_mV < 0  &&  batt_mA < 0)
		Log ("BATT_INP: Battery Controller Read Error");
	    else
		Log ("BATT_INP: %2d.%dV %4dmA",
		     (batt_mV / 1000), (batt_mV % 1000) / 100, batt_mA);
	}

	/* finally clear the flag for the next run */
	flgLogBATT = false;
    }
}


/***************************************************************************//**
 *
 * @brief	Inform the control module about a new transponder ID
 *
 * This routine must be called to inform the control module about a new
 * transponder ID.
 *
 ******************************************************************************/
void	ControlUpdateID (char *transponderID)
{
char	 line[120];
char	*pStr;
#if 0	// Currently there are no actions planned for specific birds
ID_PARM	*pID;
#endif

    pStr = line;

#if 0	// Currently there are no actions planned for specific birds
    pID = CfgLookupID (transponderID);
    if (pID == NULL)
    {
	/* Specified ID not found, look for an "ANY" entry */
	pID = CfgLookupID ("ANY");
	if (pID == NULL)
	{
	    /* No "ANY" entry defined, treat ID as "UNKNOWN" */
	    pID = CfgLookupID ("UNKNOWN");
	    if (pID == NULL)
	    {
		/* Even no "UNKNOWN" entry exists - abort */
		Log ("Transponder: %s not found - aborting", transponderID);
		return;
	    }
	    else
	    {
		pStr += sprintf (pStr, "Transponder: %s not found -"
				 " using UNKNOWN", transponderID);
	    }
	}
	else
	{
	    pStr += sprintf (pStr, "Transponder: %s not found -"
			     " using ANY", transponderID);
	}
    }
    else
#endif
    {
	pStr += sprintf (pStr, "Transponder: %s", transponderID);
    }

#ifdef LOGGING
    Log (line);
#endif
}


/***************************************************************************//**
 *
 * @brief	Power-Fail Handler for Control Module
 *
 * This function will be called in case of power-fail to switch off devices
 * that consume too much power, e.g. the camera.
 *
 ******************************************************************************/
void	ControlPowerFailHandler (void)
{
int	i;

#ifdef LOGGING
    /* Generate Log Message */
    Log ("Switching all power outputs OFF");
#endif

    /* Switch off all power outputs immediately */
    for (i = 0;  i < NUM_PWR_OUT;  i++)
	PowerOutput ((PWR_OUT)i, PWR_OFF);
}


/******************************************************************************
 *
 * @brief	Switch the specified power output on or off
 *
 * This routine enables or disables the specified power output.
 *
 * @param[in] output
 *	Power output to be changed.
 *
 * @param[in] enable
 *	If true (PWR_ON), the power output will be enabled, false (PWR_OFF)
 *	disables it.
 *
 *****************************************************************************/
void	PowerOutput (PWR_OUT output, bool enable)
{
int	m;

    /* No power enable if Power Fail is active */
    if (enable  &&  IsPowerFail())
	return;

    /* Parameter check */
    if (output == PWR_OUT_NONE)
	return;		// power output not assigned, nothing to be done

    if ((PWR_OUT)0 > output  ||  output >= NUM_PWR_OUT)
    {
#ifdef LOGGING
	/* Generate Error Log Message */
	LogError ("PowerOutput(%d, %d): Invalid output parameter",
		  output, enable);
#endif
	return;
    }

    /* See if Power Output is already in the right state */
    if ((bool)*l_PwrOutDef[output].BitBandAddr == enable)
	return;		// Yes - nothing to be done

    /* Switch power output on or off */
    *l_PwrOutDef[output].BitBandAddr = enable;

#ifdef LOGGING
    Log ("Power Output %s %sabled",
	 g_enum_PowerOutput[output], enable ? "en":"dis");
#endif

    /* Check if there exists an associated measuring facility */
    m = l_PwrOutDef[output].Measure;
    if (m != MEASURE_NONE)
    {
	/* Enable or disable measuring of this output */
	if (enable)
	{
	    /* Be sure to cancel power-off timer */
	    if (l_MeasureDef[m].hdlFollowUpTime != NONE)
		sTimerCancel(l_MeasureDef[m].hdlFollowUpTime);

	    /* Enable measuring immediately */
	    *l_MeasureDef[m].BitBandAddr = 1;

	    /* Add associated channels to bit mask */
	    Bit(l_ADC_ActiveChanMask, l_MeasureDef[m].ChanU) = 1;
	    Bit(l_ADC_ActiveChanMask, l_MeasureDef[m].ChanI) = 1;

	    /* Invalidate previous voltage and current value */
	    l_prev_value_mV[m] = l_prev_value_mA[m] = 0;

	    /* Set flag to set up and start ADC */
	    l_flgADC_On = true;

	    g_flgIRQ = true;	// keep on running
	}
	else
	{
	    /* Keep measuring another <FollowUpTime> seconds active */
	    sTimerStart(l_MeasureDef[m].hdlFollowUpTime,
			l_MeasureDef[m].FollowUpTime);
	}
    }

    /* Always initialize values for Battery measurement */
    if (enable)
	l_prev_BATT_mV = l_prev_BATT_mA = 0;

    /* Also keep measuring another <FollowUpTime> seconds active for BATT */
    if (output == PWR_OUT_BATT)
    {
	if (enable)
	{
	    sTimerCancel(l_hdlFollowUpTimeBATT);   // be sure to cancel timer
#ifdef LOGGING
	    Log ("BATT_INP: Starting Measuring");
#endif
	    l_BATT_MeasureInterval = 0;	// NO delay for first measurement
	    l_flgLogBATT = true;	// set BATT_INP measuring flag
	}
	else
	{
	    sTimerStart(l_hdlFollowUpTimeBATT, l_BATT_FollowUpTime);
	}
    }

    /* Update LC-Display if actual Power Status is displayed */
    DisplayUpdate(UPD_POWERSTATUS);
}


/******************************************************************************
 *
 * @brief	Determine if the specified power output is switched on
 *
 * This routine determines the current state of a power output.
 *
 * @param[in] output
 *	Power output to be checked.
 *
 *****************************************************************************/
bool	IsPowerOutputOn (PWR_OUT output)
{
    /* Parameter check */
    if (output == PWR_OUT_NONE)
	return false;	// power output not assigned, return false (off)

    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_BATT);

    /* Determine the current state of this power output */
    return (*l_PwrOutDef[output].BitBandAddr ? true : false);
}


/***************************************************************************//**
 *
 * @brief	Alarm routine for Power Control
 *
 * This routine is called when one of the power alarm times has been reached.
 * The alarm number is an enum value between @ref FIRST_POWER_ALARM and
 * @ref LAST_POWER_ALARM.<br>
 * When an RFID reader has been installed, the function decides whether to
 * call RFID_Enable(), RFID_Disable(), or PowerOutput() directly.
 *
 ******************************************************************************/
static void	AlarmPowerControl (int alarmNum)
{
PWR_OUT	 pwrOut;
int	 pwrState;

    /* Parameter check */
    EFM_ASSERT (FIRST_POWER_ALARM <= alarmNum && alarmNum <= LAST_POWER_ALARM);

    /* Determine switching state */
    pwrState = (alarmNum >= ALARM_UA1_OFF_TIME_1 ? PWR_OFF : PWR_ON);

    /* Determine Power Output */
    if (alarmNum >= ALARM_BATT_OFF_TIME_1)
    {
	pwrOut = PWR_OUT_BATT;
    }
    else if (alarmNum >= ALARM_UA2_OFF_TIME_1)
    {
	pwrOut = PWR_OUT_UA2;
    }
    else if (alarmNum >= ALARM_UA1_OFF_TIME_1)
    {
	pwrOut = PWR_OUT_UA1;
    }
    else if (alarmNum >= ALARM_BATT_ON_TIME_1)
    {
	pwrOut = PWR_OUT_BATT;
    }
    else if (alarmNum >= ALARM_UA2_ON_TIME_1)
    {
	pwrOut = PWR_OUT_UA2;
    }
    else
    {
	pwrOut = PWR_OUT_UA1;
    }

    /* If configured, initiate Power Interval */
    if (pwrState == PWR_OFF)
    {   // inhibit further power switching
	sTimerCancel (l_hdlPwrInterval[pwrOut]);
    }
    else
    {   // start new power interval
	if (g_On_Duration[pwrOut] >= MIN_VAL_ON_DURATION)
	    sTimerStart(l_hdlPwrInterval[pwrOut], g_On_Duration[pwrOut]);
    }

    /* See if this Power Output is used by the RFID reader */
    if (pwrOut == g_RFID_Power)
    {
	/* Power Output is used by the RFID reader - enable or disable it */
	if (pwrState == PWR_ON)
	    RFID_Enable();
	else
	    RFID_Disable();
    }
    else
    {
	/* Standard Power Output - call PowerOutput() directly */
	PowerOutput (pwrOut, pwrState);
    }

    g_flgIRQ = true;	// keep on running
}


/***************************************************************************//**
 *
 * @brief	Interval Timer routine for Power Control
 *
 * This routine is called by one of the interval timers.  The timer handle is
 * used to identify the Power Output, which can be @ref PWR_OUT_UA1, @ref
 * PWR_OUT_UA2, or @ref PWR_OUT_BATT.  The power interval consists of a
 * <b>XXX_ON_DURATION</b> seconds on phase and a <b>XXX_INTERVAL</b> minus
 * <b>XXX_ON_DURATION</b> off phase.
 * When an RFID reader has been installed, the function decides whether to
 * call RFID_Enable(), RFID_Disable(), or PowerOutput() directly.
 *
 ******************************************************************************/
static void	IntervalPowerControl (TIM_HDL hdl)
{
PWR_OUT	 pwrOut;
int	 pwrState;

    /* Determine Power Output */
    if (hdl == l_hdlPwrInterval[PWR_OUT_UA1])
    {
	pwrOut = PWR_OUT_UA1;
    }
    else if (hdl == l_hdlPwrInterval[PWR_OUT_UA2])
    {
	pwrOut = PWR_OUT_UA2;
    }
    else if (hdl == l_hdlPwrInterval[PWR_OUT_BATT])
    {
	pwrOut = PWR_OUT_BATT;
    }
    else
    {
	LogError ("IntervalPowerControl(%d): Invalid timer handle", hdl);
	return;		// Invalid handle - abort
    }

    /* Check of Power Cycling is active for this Output */
    if (g_PwrInterval[pwrOut] < MIN_VAL_INTERVAL)
	return;		// No - immediately return

    /* Determine switching state and change phase */
    if (IsPowerOutputOn(pwrOut))
    {
	// Power is currently ON - switch it OFF for a while
	pwrState = PWR_OFF;
	sTimerStart(l_hdlPwrInterval[pwrOut],
		    g_PwrInterval[pwrOut] - g_On_Duration[pwrOut]);
    }
    else
    {
	// Power is currently OFF - switch it ON for a while
	pwrState = PWR_ON;
	sTimerStart(l_hdlPwrInterval[pwrOut], g_On_Duration[pwrOut]);
    }

    /* See if this Power Output is used by the RFID reader */
    if (pwrOut == g_RFID_Power)
    {
	/* Power Output is used by the RFID reader - enable or disable it */
	if (pwrState == PWR_ON)
	    RFID_Enable();
	else
	    RFID_Disable();
    }
    else
    {
	/* Standard Power Output - call PowerOutput() directly */
	PowerOutput (pwrOut, pwrState);
    }

    g_flgIRQ = true;	// keep on running
}


/***************************************************************************//**
 *
 * @brief	Stop measuring of UA1 or UA2 power output
 *
 * This routine is called after @ref FollowUpTime seconds are over to disable
 * further measuring of a dedicated power output.
 *
 * @warning
 * 	This function is called in interrupt context!
 *
 ******************************************************************************/
static void	MeasureStop (TIM_HDL hdl)
{
int	m;

    /* Find the right measuring facility */
    for (m = 0;  m < NUM_MEASURE;  m++)
	if (l_MeasureDef[m].hdlFollowUpTime == hdl)
	    break;

    if (m >= NUM_MEASURE)
	return;		// timer handle not found - ignore

    /* Remove associated channels from bit mask */
    Bit(l_ADC_ActiveChanMask, l_MeasureDef[m].ChanU) = 0;
    Bit(l_ADC_ActiveChanMask, l_MeasureDef[m].ChanI) = 0;

    /* Stop ADC if there are no more channels to read */
    if (l_ADC_ActiveChanMask == 0)
    {
	/* Clear flag to stop ADC */
	l_flgADC_On = false;

	g_flgIRQ = true;	// keep on running
    }

    /* Disable measuring facility */
    *l_MeasureDef[m].BitBandAddr = 0;
}


/***************************************************************************//**
 *
 * @brief	Stop measuring of a dedicated power output
 *
 * This routine is called after @ref l_BATT_FollowUpTime seconds are over to
 * disable further measuring of BATT_INP (via battery controller).
 *
 * @warning
 * 	This function is called in interrupt context!
 *
 ******************************************************************************/
static void	MeasureStopBATT (TIM_HDL hdl)
{
    (void) hdl;		// suppress compiler warning "unused parameter"

    /* Reset flag */
    l_flgLogBATT = false;

#ifdef LOGGING
    Log ("BATT_INP: Measuring stopped");
#endif
}


/***************************************************************************//**
 *
 * @brief	Set up and start ADC for measuring
 *
 * This routine initializes and starts the ADC in repetitive scan mode.
 * While the ADC is running, all per @ref l_ADC_ScanChanMask selected channels
 * will be read, but bit mask @ref l_ADC_ActiveChanMask determines, which of
 * them will be stored in data array @ref l_ADC_Value.
 *
 ******************************************************************************/
static void	ADC_ScanStart (void)
{
ADC_Init_TypeDef	init;
ADC_InitScan_TypeDef	scan;

    /* ADC requires EM1, set bit in bit mask */
    Bit(g_EM1_ModuleMask, EM1_MOD_ADC) = 1;

    /* Enable clock for ADC */
    CMU_ClockEnable(cmuClock_ADC0, true);

    /*
     * We use repetitive scan mode with the following parameters:
     *
     * -> TA  = 256 clock cycles acquisition time
     * -> RES = 12bit
     * -> N  = OVS (Oversampling)
     * -> OSR = OverSampling Ratio is 2048 samples per measurement
     * -> Number of channels is 4
     *
     * This results in ((256+12) * 2048 * 4) = 2195456 clocks per scan.
     * I/O clock (HFPERCLK) is equal HFCLK per default, i.e. exactly 32MHz.
     * The ADC clock is HFPERCLK / (prescale + 1), e.g. using an ADC clock
     * of 1MHz leads to 0.91 updates per second.  The prescaler value is
     * 7bit wide which allows 0 (/1) up to 127 (/128), but ADC clock shall
     * be 13MHz~32kHz, so the clock divider must be /3~/128 which allows
     * scan durations between 52ms up to 2200ms per channel.
     *
     * This divider can be calculated:
     *		(SCAN_DURATION[ms] * 1000) / 17152L
     */
#define ADC_CLK_CONVERSION	17152L

    init.ovsRateSel = adcOvsRateSel2048;// read channel 1024x per measuring
    init.lpfMode    = adcLPFilterRC;	// use R/C-filter
    init.warmUpMode = adcWarmupKeepADCWarm;	// keep on while ADC runs
    init.timebase   = ADC_TimebaseCalc(0);	// get current freq.
    init.prescale   = (l_ScanDuration * 1000L / ADC_CLK_CONVERSION) - 1;
    init.tailgate   = false;

    ADC_Init(ADC0, &init);

    /* Set up repetitive scan mode, use 2.5V bandgap reference voltage. */
    scan.prsSel  = adcPRSSELCh0;	// Peripheral Reflex System not used
    scan.acqTime = adcAcqTime256;	// TA=256, see above
    scan.reference  = adcRef2V5;	// 2.5V bandgap reference voltage
    scan.resolution = adcResOVS;	// enable oversampling, see above
    scan.input = l_ADC_ScanChanMask << 8; // bit mask of selected ADC channels
    scan.diff  = false;			// single ended input mode
    scan.prsEnable  = false;		// Peripheral Reflex System not used
    scan.leftAdjust = false;		// leave data right adjusted
    scan.rep = true;			// use repetitive scan mode

    ADC_InitScan(ADC0, &scan);

    /* Be sure to clear any pending interrupts */
    ADC0->IFC = _ADC_IEN_MASK;

    /* Enable interrupt for Scan Mode in ADC and NVIC */
    NVIC_SetPriority(ADC0_IRQn, INT_PRIO_ADC);
    ADC0->IEN = ADC_IEN_SCAN;
    NVIC_EnableIRQ(ADC0_IRQn);

    /* Start ADC */
    ADC_Start(ADC0, adcStartScan);
}


/***************************************************************************//**
 *
 * @brief	Stop measuring of a dedicated power output
 *
 * This routine is called after @ref FollowUpTime seconds are over to disable
 * further measuring of a dedicated power output.
 *
 ******************************************************************************/
static void	ADC_ScanStop (void)
{
    /* Disable interrupt in ADC and NVIC */
    ADC0->IEN = 0;
    NVIC_DisableIRQ(ADC0_IRQn);

    /* Reset ADC */
    ADC_Reset(ADC0);

    /* Disable clock for ADC */
    CMU_ClockEnable(cmuClock_ADC0, false);

    /* ADC is no longer active, clear bit in bit mask */
    Bit(g_EM1_ModuleMask, EM1_MOD_ADC) = 0;
}


/***************************************************************************//**
 *
 * @brief	Interrupt Handler for ADC0
 *
 * This is the interrupt handler for ADC0.  It stores the raw values of up to
 * four channels into array @ref l_ADC_Value.  The ADC is configured in scan
 * mode and runs continuously until it is stopped.
 *
 ******************************************************************************/
void ADC0_IRQHandler(void)
{
uint32_t	IntFlags;
uint32_t	status;
uint32_t	value;
int		chan;

    DEBUG_TRACE(0x03);

    /* Check cause of this interrupt */
    IntFlags = ADC0->IF;
    if (IntFlags & (ADC_IF_SCANOF | ADC_IF_SINGLEOF | ADC_IF_SINGLE))
    {
	/* Error condition - store status in debug variable */
	l_dbg_ADC_IF = IntFlags;

	ADC0->IFC = ADC_IFC_SCANOF | ADC_IFC_SINGLEOF | ADC_IFC_SINGLE;
	l_dbg_ADC_ErrCnt++;		// increase error count

	DEBUG_TRACE_STOP;		// stop trace to see what happened
    }

    /* Check for regular scan conversion complete */
    if ((IntFlags & ADC_IF_SCAN) == 0)
    {
	l_dbg_ADC_NotReadyCnt++;
	g_flgIRQ = true;	// keep on running
	DEBUG_TRACE(0x83);
	return;			// nothing to be done
    }

    /* OK, ADC_IF_SCAN is set - clear it */
    ADC0->IFC = ADC_IFC_SCAN;

    /* Check ADC status */
    status = ADC0->STATUS;
    if ((status & ADC_STATUS_SCANDV) == 0)
	l_dbg_ADC_ErrCnt++;		// increase error count

    /* See which channel has been converted this time */
    chan = (status >> 24) & 0x7;

    /* Translate channel number into index to store current value */
    chan = l_ADC_ChanIdxMap[chan];

    /* Count overflows (debugging) */
    if (IntFlags & ADC_IF_SCANOF)
	l_dbg_ADC_OvflErrCnt[chan]++;

    /* Count number of conversions for each channel (debugging) */
    l_dbg_ADC_ChanCnt[chan]++;

    /* Read current value */
    value = ADC0->SCANDATA;

    /* Store new value */
    l_ADC_Value[chan] = value;

    /* Mark update of this channel */
    Bit(l_ADC_ValueUpdateMask, chan) = 1;

    g_flgIRQ = true;	// keep on running

    DEBUG_TRACE(0x83);
}


/******************************************************************************
 *
 * @brief	Get Voltage value of Power Output in [mV]
 *
 * This routine returns the actual voltage of the specified power output
 * channel in [mV].  Only UA1 and UA2 have the ability to measure voltage.
 *
 * @param[in] output
 *	Power output to return voltage value for.
 *
 * @return
 *	Value in [mV].
 *
 *****************************************************************************/
uint32_t PowerVoltage (PWR_OUT output)
{
int	idx;

    /* Parameter check */
    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_UA2);

    /* Set index */
    idx = (output * 2);

    /* Get value of this power output and calculate to [mV] */
    return (l_ADC_Value[idx] << 16) / l_mV_Divider[output];
}


/******************************************************************************
 *
 * @brief	Get Current value of Power Output in [mA]
 *
 * This routine returns the actual current of the specified power output
 * channel in [mA].  Only UA1 and UA2 have the ability to measure current.
 *
 * @param[in] output
 *	Power output to return current value for.
 *
 * @return
 *	Value in [mA].
 *
 *****************************************************************************/
uint32_t PowerCurrent (PWR_OUT output)
{
int	idx;

    /* Parameter check */
    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_UA2);

    /* Set index */
    idx = (output * 2) + 1;

    /* Get value of this power output and calculate to [mA] */
    return (l_ADC_Value[idx] << 16) / l_mA_Divider[output];
}


/******************************************************************************
 *
 * @brief	Get ADC raw value for a given voltage in [mV]
 *
 * This routine converts a value in [mV] into a an ADC raw value according
 * to the actual conversion factor.
 *
 * @param[in] output
 *	Power output to select conversion factor for.
 *
 * @param[in] value_mV
 *	Voltage value in [mV].
 *
 * @return
 *	ADC raw value.
 *
 *****************************************************************************/
uint32_t Voltage_To_ADC_Value (PWR_OUT output, uint32_t value_mV)
{
    /* Parameter check */
    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_UA2);

    /* Get value of this power output and calculate to [mV] */
    return (value_mV * l_mV_Divider[output]) >> 16;
}


/******************************************************************************
 *
 * @brief	Get ADC raw value for a given current in [mA]
 *
 * This routine converts a value in [mA] into a an ADC raw value according
 * to the actual conversion factor.
 *
 * @param[in] output
 *	Power output to select conversion factor for.
 *
 * @param[in] value_mA
 *	Current value in [mA].
 *
 * @return
 *	ADC raw value.
 *
 *****************************************************************************/
uint32_t Current_To_ADC_Value (PWR_OUT output, uint32_t value_mA)
{
    /* Parameter check */
    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_UA2);

    /* Get value of this power output and calculate to [mA] */
    return (value_mA * l_mA_Divider[output]) >> 16;
}


/******************************************************************************
 *
 * @brief	Calibrate Voltage measurement for this output
 *
 * This routine calibrates the Voltage measurement of the specified power output
 * channel.  A reference value must be specified in [mV].
 * Only UA1 and UA2 have the ability to measure voltage.
 *
 * @param[in] output
 *	Power output to return voltage value for.
 *
 * @param[in] referenceValue_mV
 *	Reference value (the real actual voltage) in [mV].
 *
 *****************************************************************************/
void	CalibrateVoltage (PWR_OUT output, uint32_t referenceValue_mV)
{
int	idx;

    /* Parameter check */
    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_UA2);

    /* Set index */
    idx = (output * 2);

    /* Calculate divider based on the reference value */
    l_mV_Divider[output] = (l_ADC_Value[idx] << 16) / referenceValue_mV;
}


/******************************************************************************
 *
 *
 * @brief	Calibrate Current measurement for this output
 *
 * This routine calibrates the Current measurement of the specified power output
 * channel.  A reference value must be specified in [mA].
 * Only UA1 and UA2 have the ability to measure current.
 *
 * @param[in] output
 *	Power output to return voltage value for.
 *
 * @param[in] referenceValue_mA
 *	Reference value (the real actual voltage) in [mA].
 *
 *****************************************************************************/
void	CalibrateCurrent (PWR_OUT output, uint32_t referenceValue_mA)
{
int	idx;

    /* Parameter check */
    EFM_ASSERT (PWR_OUT_UA1 <= output  &&  output <= PWR_OUT_UA2);

    /* Set index */
    idx = (output * 2) + 1;

    /* Calculate divider based on the reference value */
    l_mA_Divider[output] = (l_ADC_Value[idx] << 16) / referenceValue_mA;
}


/***************************************************************************//**
 *
 * @brief	Read Calibration Data from EEPROM
 *
 * This routine reads calibration data from @ref EEPROM (FLASH) and stores
 * them into the respective variables.  If there is no valid data found, i.e.
 * the magic number or checksum is wrong, default values will be set.
 *
 ******************************************************************************/
static void	ReadCalibrationData(void)
{
uint16_t	data_h, data_l, sum=0;

#ifdef LOGGING
    Log ("Reading Calibration Values from Flash");
#endif
    EE_Read(&magic, &data_l);
    if (data_l == MAGIC_ID)
    {
	/* Verify if whole block is valid */
	sum = data_l;
	EE_Read(&ua1mV_h, &data_h);
	sum += data_h;
	EE_Read(&ua1mV_l, &data_l);
	sum += data_l;
	l_mV_Divider[0] = (uint32_t)(data_h << 16) + (uint32_t)data_l;
	EE_Read(&ua1mA_h, &data_h);
	sum += data_h;
	EE_Read(&ua1mA_l, &data_l);
	sum += data_l;
	l_mA_Divider[0] = (uint32_t)(data_h << 16) + (uint32_t)data_l;
	EE_Read(&ua2mV_h, &data_h);
	sum += data_h;
	EE_Read(&ua2mV_l, &data_l);
	sum += data_l;
	l_mV_Divider[1] = (uint32_t)(data_h << 16) + (uint32_t)data_l;
	EE_Read(&ua2mA_h, &data_h);
	sum += data_h;
	EE_Read(&ua2mA_l, &data_l);
	sum += data_l;
	l_mA_Divider[1] = (uint32_t)(data_h << 16) + (uint32_t)data_l;
	EE_Read(&chksum, &data_h);
	data_l = MAGIC_ID;
    }

    if (data_l != MAGIC_ID  ||  sum != data_h)
    {
#ifdef LOGGING
	Log ("Calibration Values: Wrong magic or checksum - using defaults");
#endif
	/* Checksum is invalid, set default data */
	l_mV_Divider[0] = l_mA_Divider[0] = l_mV_Divider[1]
			= l_mA_Divider[1] = (1L << 16);
    }
}


/***************************************************************************//**
 *
 * @brief	Write Calibration Data to EEPROM
 *
 * This routine writes the calibration data to @ref EEPROM (FLASH).  To protect
 * the data, a magic word and a checksum are stored additionally.
 *
 ******************************************************************************/
void	WriteCalibrationData(void)
{
uint16_t	data, sum;

    INT_Disable();	// disable IRQs during FLASH programming

    /* store adjustments into non-volatile memory */
    sum = data = MAGIC_ID;
    EE_Write(&magic, data);

    data = (uint16_t)((l_mV_Divider[0] >> 16) & 0xFFFF);
    sum += data;
    EE_Write(&ua1mV_h, data);
    data = (uint16_t)(l_mV_Divider[0] & 0xFFFF);
    sum += data;
    EE_Write(&ua1mV_l, data);

    data = (uint16_t)((l_mA_Divider[0] >> 16) & 0xFFFF);
    sum += data;
    EE_Write(&ua1mA_h, data);
    data = (uint16_t)(l_mA_Divider[0] & 0xFFFF);
    sum += data;
    EE_Write(&ua1mA_l, data);

    data = (uint16_t)((l_mV_Divider[1] >> 16) & 0xFFFF);
    sum += data;
    EE_Write(&ua2mV_h, data);
    data = (uint16_t)(l_mV_Divider[1] & 0xFFFF);
    sum += data;
    EE_Write(&ua2mV_l, data);

    data = (uint16_t)((l_mA_Divider[1] >> 16) & 0xFFFF);
    sum += data;
    EE_Write(&ua2mA_h, data);
    data = (uint16_t)(l_mA_Divider[1] & 0xFFFF);
    sum += data;
    EE_Write(&ua2mA_l, data);

    EE_Write(&chksum, sum);

    INT_Enable();

#ifdef LOGGING
    Log ("Calibration Values have been saved to Flash");
#endif
}
