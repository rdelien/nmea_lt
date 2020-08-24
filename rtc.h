#ifndef	RTC_H
#define	RTC_H


/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/
#define EPOCH_YEAR              2000


/******************************************************************************/
/* Types                                                                      */
/******************************************************************************/
#ifdef __x86_64__
typedef	unsigned int   rtcsecs_t;
#else
typedef	unsigned long  rtcsecs_t;
#endif

struct rtctime_t {
	unsigned char  sec;   /* 0...59 & 60 */
	unsigned char  min;   /* 0...59 */
	unsigned char  hour;  /* 0...23 */
	unsigned char  day;   /* 1...31 */
	unsigned char  mon;   /* 0...11 */
	unsigned char  year;  /* 0...105 (for 2000...2105) */
};


/******************************************************************************/
/* Functions                                                                  */
/******************************************************************************/
int           rtc_time2secs(struct rtctime_t  *rtctime,
                            rtcsecs_t         *rtcsecs);
void          rtc_secs2time(rtcsecs_t         rtcsecs,
                            struct rtctime_t  *rtctime);
unsigned char rtc_weekday  (rtcsecs_t         rtcsecs);


#endif	/* RTC_H */