#include "wti_log.h"

const char *domain = "wl-textinput";
int _wti_log_domain = -1;

EINTERN Eina_Bool
wti_log_init(void)
{
   _wti_log_domain = eina_log_domain_register(domain, EINA_COLOR_LIGHTCYAN);
   if (_wti_log_domain < 0)
     {
        EINA_LOG_ERR("Unable to register '%s' log domain", domain);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN void
wti_log_shutdown(void)
{
   eina_log_domain_unregister(_wti_log_domain);
   _wti_log_domain = -1;
}
