#ifndef _WTI_LOG_H_
#define _WTI_LOG_H_

#include <e.h>

#ifdef DBG
#undef DBG
#endif
#ifdef INF
#undef INF
#endif
#ifdef WRN
#undef WRN
#endif
#ifdef ERR
#undef ERR
#endif
#ifdef CRI
#undef CRI
#endif

#ifndef WTI_LOG
#define WTI_LOG(...) printf(__VA_ARGS__)
#endif

extern EINTERN int _wti_log_domain;
#define DBG(...)     EINA_LOG_DOM_DBG(_wti_log_domain, __VA_ARGS__)
#define INF(...)     EINA_LOG_DOM_INFO(_wti_log_domain, __VA_ARGS__)
#define WRN(...)     EINA_LOG_DOM_WARN(_wti_log_domain, __VA_ARGS__)
#define ERR(...)     EINA_LOG_DOM_ERR(_wti_log_domain, __VA_ARGS__)
#define CRI(...)     EINA_LOG_DOM_CRIT(_wti_log_domain, __VA_ARGS__)

EINTERN Eina_Bool wti_log_init(void);
EINTERN void      wti_log_shutdown(void);

#endif
