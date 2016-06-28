#ifndef _E_MOD_MAIN_H
#define _E_MOD_MAIN_H

#include "wti_log.h"

Eina_Bool   e_input_panel_init(void);
void        e_input_panel_shutdown(void);
void        e_input_panel_visibility_change(Eina_Bool visible);
Eina_Bool   e_input_panel_client_find(E_Client *ec);

#endif
