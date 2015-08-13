#define E_COMP_WL
#include "e.h"
#include "e_mod_main.h"
#include <text-server-protocol.h>
#include <input-method-server-protocol.h>
#include "Eeze.h"

# define E_LIST_FILTER_HANDLER_APPEND(list, callback, data) \
  do \
    { \
       Ecore_Event_Filter *_eh; \
       _eh = ecore_event_filter_add(NULL, (Ecore_Filter_Cb)callback, NULL, data); \
       assert(_eh); \
       list = eina_list_append(list, _eh); \
    } \
  while (0)

typedef struct _E_Text_Input E_Text_Input;
typedef struct _E_Text_Input_Mgr E_Text_Input_Mgr;
typedef struct _E_Input_Method E_Input_Method;
typedef struct _E_Input_Method_Context E_Input_Method_Context;
typedef struct _E_Mod_Text_Input_Shutdown_Cb E_Mod_Text_Input_Shutdown_Cb;

struct _E_Text_Input
{
   struct wl_resource *resource;

   E_Comp_Data *cdata;
   Eina_List *input_methods;
   Eina_Bool input_panel_visibile;
};

struct _E_Text_Input_Mgr
{
   struct wl_global *global;
   struct wl_resource *resource;

   E_Comp_Data *cdata;
   Eina_List *text_input_list;
};

struct _E_Input_Method
{
   struct wl_global *global;
   struct wl_resource *resource;

   E_Comp_Data *cdata;
   E_Text_Input *model;
   E_Input_Method_Context *context;
};

struct _E_Input_Method_Context
{
   struct wl_resource *resource;

   E_Text_Input *model;
   E_Input_Method *input_method;

   struct
   {
      struct wl_resource *resource;
      Eina_List *handlers;
      Eina_Bool grabbed;
   } kbd;
};

struct _E_Mod_Text_Input_Shutdown_Cb
{
   void (*func)(void *data);
   void *data;
};

static E_Input_Method *g_input_method = NULL;
static Eina_List *shutdown_list = NULL;
static Eina_Bool g_keyboard_connecting = EINA_FALSE;
static Eeze_Udev_Watch *eeze_udev_watch_hander = NULL;
static Ecore_Event_Handler *ecore_key_down_handler = NULL;

static void
_e_text_input_method_context_keyboard_grab_keyboard_state_update(E_Comp_Data *cdata, uint32_t keycode, Eina_Bool pressed)
{
   enum xkb_key_direction dir;

   if (!cdata->xkb.state) return;

   if (pressed) dir = XKB_KEY_DOWN;
   else dir = XKB_KEY_UP;

   cdata->kbd.mod_changed =
     xkb_state_update_key(cdata->xkb.state, keycode + 8, dir);
}

static void
_e_text_input_method_context_keyboard_grab_keyboard_modifiers_update(E_Comp_Data *cdata, struct wl_resource *keyboard)
{
   uint32_t serial;

   if (!cdata->xkb.state) return;

   cdata->kbd.mod_depressed =
     xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_DEPRESSED);
   cdata->kbd.mod_latched =
     xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_MODS_LATCHED);
   cdata->kbd.mod_locked =
     xkb_state_serialize_mods(cdata->xkb.state, XKB_STATE_MODS_LOCKED);
   cdata->kbd.mod_group =
     xkb_state_serialize_layout(cdata->xkb.state, XKB_STATE_LAYOUT_EFFECTIVE);

   serial = wl_display_next_serial(cdata->wl.disp);
   wl_keyboard_send_modifiers(keyboard, serial,
                              cdata->kbd.mod_depressed,
                              cdata->kbd.mod_latched,
                              cdata->kbd.mod_locked,
                              cdata->kbd.mod_group);
}

static void
_e_text_input_method_context_keyboard_grab_key(void *data, void *event, Eina_Bool pressed)
{
   E_Input_Method_Context *context = NULL;
   E_Comp_Data *cdata = NULL;
   Ecore_Event_Key *ev;
   uint32_t serial, keycode;

   if (!(context = data)) return;
   if (!(context->model)) return;
   if (!(cdata = context->model->cdata)) return;
   if (!(ev = event)) return;

   keycode = ev->keycode - 8;

   /* update modifier state */
   _e_text_input_method_context_keyboard_grab_keyboard_state_update(cdata, keycode, pressed);

   serial = wl_display_next_serial(cdata->wl.disp);
   if (pressed == EINA_TRUE)
     {
        wl_keyboard_send_key(context->kbd.resource, serial, ev->timestamp,
                             keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
     }
   else
     {
        wl_keyboard_send_key(context->kbd.resource, serial, ev->timestamp,
                             keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
     }
   if (cdata->kbd.mod_changed)
     {
        _e_text_input_method_context_keyboard_grab_keyboard_modifiers_update(cdata, context->kbd.resource);
        cdata->kbd.mod_changed = 0;
     }
   return;
}

static Eina_Bool
_e_text_input_method_context_keyboard_grab_event_filter(void *data __UNUSED__, void *loop_data,int type, void *event __UNUSED__)
{

   if (type == ECORE_EVENT_KEY_DOWN)
     {
        _e_text_input_method_context_keyboard_grab_key(data, event, EINA_TRUE);
        return EINA_FALSE;
     }
   else if (type == ECORE_EVENT_KEY_UP)
     {
        _e_text_input_method_context_keyboard_grab_key(data, event, EINA_FALSE);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_text_input_method_context_grab_set(E_Input_Method_Context *context, Eina_Bool set)
{
   if (set == context->kbd.grabbed)
     return;

   context->kbd.grabbed = set;

   if (set)
     {
        E_LIST_FILTER_HANDLER_APPEND(context->kbd.handlers,
                                     _e_text_input_method_context_keyboard_grab_event_filter,
                                     context);
        e_comp_grab_input(e_comp, 0, 1);
     }
   else
     {
        E_FREE_LIST(context->kbd.handlers, ecore_event_filter_del);
        e_comp_ungrab_input(e_comp, 0, 1);
     }
}

static void
_e_mod_text_input_shutdown_cb_add(void (*func)(void *data), void *data)
{
   E_Mod_Text_Input_Shutdown_Cb *cb;

   if (!(cb = E_NEW(E_Mod_Text_Input_Shutdown_Cb, 1)))
     {
        ERR("Could not allocate space for Text Input Shutdown Callback");
        return;
     }

   cb->func = func;
   cb->data = data;

   shutdown_list = eina_list_append(shutdown_list, cb);
}

static void
_e_text_input_method_context_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_text_input_method_context_cb_string_commit(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial, const char *text)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_commit_string(context->model->resource,
                                      serial, text);
}

static void
_e_text_input_method_context_cb_preedit_string(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial, const char *text, const char *commit)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_preedit_string(context->model->resource,
                                       serial, text, commit);
}

static void
_e_text_input_method_context_cb_preedit_styling(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t index, uint32_t length, uint32_t style)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_preedit_styling(context->model->resource,
                                        index, length, style);
}

static void
_e_text_input_method_context_cb_preedit_cursor(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t cursor)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_preedit_cursor(context->model->resource,
                                       cursor);
}

static void
_e_text_input_method_context_cb_surrounding_text_delete(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t index, uint32_t length)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_delete_surrounding_text(context->model->resource,
                                                index, length);
}

static void
_e_text_input_method_context_cb_cursor_position(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t index, int32_t anchor)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_cursor_position(context->model->resource,
                                        index, anchor);
}

static void
_e_text_input_method_context_cb_modifiers_map(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_array *map)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_modifiers_map(context->model->resource, map);
}

static void
_e_text_input_method_context_cb_keysym(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial, uint32_t time, uint32_t sym, uint32_t state, uint32_t modifiers)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_keysym(context->model->resource,
                               serial, time, sym, state, modifiers);
}

static void
_e_text_input_method_context_keyboard_grab_cb_resource_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface _e_keyboard_grab_interface =
{
   _e_text_input_method_context_keyboard_grab_cb_resource_destroy
};

static void
_e_text_input_method_context_keyboard_grab_cb_keyboard_unbind(struct wl_resource *resource)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   _e_text_input_method_context_grab_set(context, EINA_FALSE);

   context->kbd.resource = NULL;
}

static void
_e_text_input_method_context_cb_keyboard_grab(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   DBG("Input Method Context - grab keyboard %d", wl_resource_get_id(resource));
   E_Input_Method_Context *context  = wl_resource_get_user_data(resource);
   struct wl_resource *keyboard = NULL;
   E_Comp_Data *cdata = NULL;
   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }
   keyboard = wl_resource_create(client, &wl_keyboard_interface, 1, id);
   if (!keyboard)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(keyboard, &_e_keyboard_grab_interface, context, _e_text_input_method_context_keyboard_grab_cb_keyboard_unbind);

   if (context->model && context->model->cdata)
     {
        cdata = context->model->cdata;
        /* send current keymap */
        wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           cdata->xkb.fd, cdata->xkb.size);
     }

   context->kbd.resource = keyboard;

   _e_text_input_method_context_grab_set(context, EINA_TRUE);
}

static void
_e_text_input_method_context_cb_key(struct wl_client *client, struct wl_resource *resource, uint32_t serial, uint32_t time, uint32_t key, uint32_t state_w)
{
   DBG("Input Method Context - key %d", wl_resource_get_id(resource));

   (void)client;
   (void)resource;
   (void)serial;
   (void)time;
   (void)key;
   (void)state_w;
}

static void
_e_text_input_method_context_cb_modifiers(struct wl_client *client, struct wl_resource *resource, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
   DBG("Input Method Context - modifiers %d", wl_resource_get_id(resource));

   (void)client;
   (void)resource;
   (void)serial;
   (void)mods_depressed;
   (void)mods_latched;
   (void)mods_locked;
   (void)group;
}

static void
_e_text_input_method_context_cb_language(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial, const char *language)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_language(context->model->resource,
                                 serial, language);
}

static void
_e_text_input_method_context_cb_text_direction(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial, uint32_t direction)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if ((context->model) && (context->model->resource))
     wl_text_input_send_text_direction(context->model->resource,
                                       serial, direction);
}

static const struct wl_input_method_context_interface _e_text_input_method_context_implementation = {
     _e_text_input_method_context_cb_destroy,
     _e_text_input_method_context_cb_string_commit,
     _e_text_input_method_context_cb_preedit_string,
     _e_text_input_method_context_cb_preedit_styling,
     _e_text_input_method_context_cb_preedit_cursor,
     _e_text_input_method_context_cb_surrounding_text_delete,
     _e_text_input_method_context_cb_cursor_position,
     _e_text_input_method_context_cb_modifiers_map,
     _e_text_input_method_context_cb_keysym,
     _e_text_input_method_context_cb_keyboard_grab,
     _e_text_input_method_context_cb_key,
     _e_text_input_method_context_cb_modifiers,
     _e_text_input_method_context_cb_language,
     _e_text_input_method_context_cb_text_direction
};

static void
_e_text_input_method_context_cb_resource_destroy(struct wl_resource *resource)
{
   E_Input_Method_Context *context = wl_resource_get_user_data(resource);

   if (!context)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method Context For Resource");
        return;
     }

   if (context->kbd.resource)
     wl_resource_destroy(context->kbd.resource);

   if ((context->input_method) &&
       (context->input_method->context == context))
     context->input_method->context = NULL;

   free(context);
}

static void
_e_text_input_deactivate(E_Text_Input *text_input, E_Input_Method *input_method)
{
   if (input_method->model == text_input)
     {
        if ((input_method->context) && (input_method->resource))
          {
             _e_text_input_method_context_grab_set(input_method->context,
                                                   EINA_FALSE);
             // TODO: finish the grab of keyboard.
             wl_input_method_send_deactivate(input_method->resource,
                                             input_method->context->resource);
          }

        input_method->model = NULL;
        if (input_method->context) input_method->context->model = NULL;
        input_method->context = NULL;

        text_input->input_methods = eina_list_remove(text_input->input_methods, input_method);

        if (text_input->resource)
          wl_text_input_send_leave(text_input->resource);
     }
}

static void
_e_text_input_cb_activate(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, struct wl_resource *surface)
{
   E_Text_Input *text_input = NULL;
   E_Input_Method *input_method = NULL;
   E_Text_Input *old = NULL;
   E_Input_Method_Context *context = NULL;

   EINA_SAFETY_ON_NULL_GOTO(resource, err);
   EINA_SAFETY_ON_NULL_GOTO(seat, err);
   EINA_SAFETY_ON_NULL_GOTO(g_input_method, err);
   EINA_SAFETY_ON_NULL_GOTO(g_input_method->resource, err);

   text_input = wl_resource_get_user_data(resource);

   // FIXME: should get input_method object from seat.
   input_method = wl_resource_get_user_data(g_input_method->resource);
   EINA_SAFETY_ON_NULL_GOTO(input_method, err);

   old = input_method->model;
   if (old == text_input)
     return;

   if (old)
     _e_text_input_deactivate(old, input_method);

   input_method->model = text_input;
   text_input->input_methods = eina_list_append(text_input->input_methods, input_method);

   if (input_method->resource)
     {
        if (!(context = E_NEW(E_Input_Method_Context, 1)))
          {
             wl_client_post_no_memory(client);
             ERR("Could not allocate space for Input_Method_Context");
             return;
          }

        context->resource =
           wl_resource_create(wl_resource_get_client(input_method->resource),
                              &wl_input_method_context_interface, 1, 0);

        wl_resource_set_implementation(context->resource,
                                       &_e_text_input_method_context_implementation,
                                       context, _e_text_input_method_context_cb_resource_destroy);

        context->model = text_input;
        context->input_method = input_method;
        input_method->context = context;

        wl_input_method_send_activate(input_method->resource, context->resource);
     }

   if (text_input->input_panel_visibile)
     e_input_panel_visibility_change(EINA_TRUE);

   if (text_input->resource)
     wl_text_input_send_enter(text_input->resource, surface);

   return;

err:
   if (resource)
     wl_resource_post_error(resource,
                            WL_DISPLAY_ERROR_INVALID_OBJECT,
                            "No Text Input For Resource");
   if (seat)
     wl_resource_post_error(seat,
                            WL_DISPLAY_ERROR_INVALID_OBJECT,
                            "No Comp Data For Seat");
}
static void
_e_text_input_cb_deactivate(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *seat)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Comp_Data *cdata = wl_resource_get_user_data(seat);
   E_Input_Method *input_method = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   if (!cdata)
     {
        wl_resource_post_error(seat,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Comp Data For Seat");
        return;
     }

   // FIXME: should get input_method object from seat.
   if (g_input_method && g_input_method->resource)
     input_method = wl_resource_get_user_data(g_input_method->resource);

   if (!input_method)
     {
        wl_resource_post_error(seat,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method For Seat");
        return;
     }

   _e_text_input_deactivate(text_input, input_method);
}

static void
_e_text_input_cb_input_panel_show(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   if (g_keyboard_connecting == EINA_TRUE)
     return;

   text_input->input_panel_visibile = EINA_TRUE;

   e_input_panel_visibility_change(EINA_TRUE);

   if (text_input->resource)
     wl_text_input_send_input_panel_state(text_input->resource,
                                          WL_TEXT_INPUT_INPUT_PANEL_STATE_SHOW);
}

static void
_e_text_input_cb_input_panel_hide(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   text_input->input_panel_visibile = EINA_FALSE;

   if (text_input->resource)
     wl_text_input_send_input_panel_state(text_input->resource,
                                          WL_TEXT_INPUT_INPUT_PANEL_STATE_HIDE);

   e_input_panel_visibility_change(EINA_FALSE);
}

static void
_e_text_input_cb_reset(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;
        if (input_method->context->resource)
          wl_input_method_context_send_reset(input_method->context->resource);
     }
}

static void
_e_text_input_cb_surrounding_text_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *text, uint32_t cursor, uint32_t anchor)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;
        if (input_method->context->resource)
          wl_input_method_context_send_surrounding_text(input_method->context->resource,
                                                        text, cursor, anchor);
     }
}

static void
_e_text_input_cb_content_type_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t hint, uint32_t purpose)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;

        if (input_method->context->resource)
          wl_input_method_context_send_content_type(input_method->context->resource,
                                                    hint, purpose);
     }
}

static void
_e_text_input_cb_cursor_rectangle_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   // TODO: issue event update input_panel
}

static void
_e_text_input_cb_preferred_language_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, const char *language)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;

        if (input_method->context->resource)
          wl_input_method_context_send_preferred_language(input_method->context->resource,
                                                          language);
     }
}

static void
_e_text_input_cb_state_commit(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t serial)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;

        if (input_method->context->resource)
          wl_input_method_context_send_commit_state(input_method->context->resource, serial);
     }
}

static void
_e_text_input_cb_action_invoke(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t button, uint32_t index)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;

        if (input_method->context->resource)
          wl_input_method_context_send_invoke_action(input_method->context->resource,
                                                     button, index);
     }
}

static void
_e_text_input_cb_return_key_type_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t return_key_type)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;

        if (input_method->context->resource)
          wl_input_method_context_send_return_key_type(input_method->context->resource,
                                                       return_key_type);
     }
}

static void
_e_text_input_cb_return_key_disabled_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t disabled)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;
   Eina_List *l = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FOREACH(text_input->input_methods, l, input_method)
     {
        if (!input_method || !input_method->context) continue;

        if (input_method->context->resource)
          wl_input_method_context_send_return_key_disabled(input_method->context->resource,
                                                           disabled);
     }
}

static const struct wl_text_input_interface _e_text_input_implementation = {
     _e_text_input_cb_activate,
     _e_text_input_cb_deactivate,
     _e_text_input_cb_input_panel_show,
     _e_text_input_cb_input_panel_hide,
     _e_text_input_cb_reset,
     _e_text_input_cb_surrounding_text_set,
     _e_text_input_cb_content_type_set,
     _e_text_input_cb_cursor_rectangle_set,
     _e_text_input_cb_preferred_language_set,
     _e_text_input_cb_state_commit,
     _e_text_input_cb_action_invoke,
     _e_text_input_cb_return_key_type_set,
     _e_text_input_cb_return_key_disabled_set
};

static void
_e_text_input_cb_destroy(struct wl_resource *resource)
{
   E_Text_Input *text_input = wl_resource_get_user_data(resource);
   E_Input_Method *input_method = NULL;

   if (!text_input)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input For Resource");
        return;
     }

   EINA_LIST_FREE(text_input->input_methods, input_method)
      _e_text_input_deactivate(text_input, input_method);

   e_input_panel_visibility_change(EINA_FALSE);

   free(text_input);
}

static void
_e_text_input_manager_cb_text_input_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Text_Input_Mgr *text_input_mgr = wl_resource_get_user_data(resource);
   E_Text_Input *text_input = NULL;

   if (!text_input_mgr)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Text Input Manager For Resource");
        return;
     }

   if (!(text_input = E_NEW(E_Text_Input, 1)))
     {
        wl_client_post_no_memory(client);
        ERR("Could not allocate space for Text_Input");
        return;
     }

   text_input->resource =
      wl_resource_create(client, &wl_text_input_interface, 1, id);

   if (!text_input->resource)
     {
        wl_client_post_no_memory(client);
        ERR("Could not create the resource for text_input");
        free(text_input);
        return;
     }

   wl_resource_set_implementation(text_input->resource,
                                  &_e_text_input_implementation,
                                  text_input, _e_text_input_cb_destroy);

   text_input->cdata = text_input_mgr->cdata;
}

static const struct wl_text_input_manager_interface _e_text_input_manager_implementation = {
     _e_text_input_manager_cb_text_input_create
};

static void
_e_text_cb_bind_text_input_manager(struct wl_client *client, void *data, uint32_t version EINA_UNUSED, uint32_t id)
{
   E_Text_Input_Mgr *text_input_mgr = data;

   text_input_mgr->resource =
      wl_resource_create(client,
                         &wl_text_input_manager_interface, 1, id);

   if (text_input_mgr->resource)
     wl_resource_set_implementation(text_input_mgr->resource,
                                    &_e_text_input_manager_implementation,
                                    text_input_mgr, NULL);
}

static void
_e_text_input_method_cb_unbind(struct wl_resource *resource)
{
   E_Input_Method *input_method = wl_resource_get_user_data(resource);

   if (!input_method)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Method For Resource");
        return;
     }

   if (input_method->model)
     _e_text_input_deactivate(input_method->model, input_method);

   input_method->resource = NULL;
   input_method->context = NULL;
}

static void
_e_text_cb_bind_input_method(struct wl_client *client, void *data, uint32_t version EINA_UNUSED, uint32_t id)
{
   E_Input_Method *input_method = data;
   struct wl_resource *resource = NULL;

   if (!input_method) return;

   resource = wl_resource_create(client, &wl_input_method_interface, 1, id);

   if (input_method->resource)
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "interface object already bound");
        return;
     }

   input_method->resource = resource;

   wl_resource_set_implementation(resource, NULL, input_method,
                                  _e_text_input_method_cb_unbind);
}

static void
_e_text_input_method_destroy(void *data)
{
   E_Input_Method *input_method = data;

   if (!input_method)
     return;

   if (input_method->global)
     wl_global_destroy(input_method->global);

   free(input_method);
}

static Eina_Bool
_e_text_input_method_create(E_Comp_Data *cdata)
{
   if (!cdata) return EINA_FALSE;

   if (!(g_input_method = E_NEW(E_Input_Method, 1)))
     {
        ERR("Could not allocate space for Input_Method");
        return EINA_FALSE;
     }

   g_input_method->cdata = cdata;
   g_input_method->global =
      wl_global_create(cdata->wl.disp, &wl_input_method_interface, 1,
                       g_input_method, _e_text_cb_bind_input_method);

   if (!g_input_method->global)
     {
        free(g_input_method);
        g_input_method = NULL;
        return EINA_FALSE;
     }

   _e_mod_text_input_shutdown_cb_add(_e_text_input_method_destroy, g_input_method);

   return EINA_TRUE;
}

static void
_e_text_input_manager_destroy(void *data)
{
   E_Text_Input_Mgr *text_input_mgr = data;
   if (!text_input_mgr) return;

   wl_global_destroy(text_input_mgr->global);
   free(text_input_mgr);
}

static Eina_Bool
_e_text_input_manager_create(E_Comp_Data *cdata)
{
   E_Text_Input_Mgr *text_input_mgr;

   if (!cdata) return EINA_FALSE;

   if (!(text_input_mgr = E_NEW(E_Text_Input_Mgr, 1)))
     {
        ERR("Could not allocate space for Text_Input_Manager");
        return EINA_FALSE;
     }

   text_input_mgr->cdata = cdata;
   text_input_mgr->global =
      wl_global_create(cdata->wl.disp,
                       &wl_text_input_manager_interface, 1,
                       text_input_mgr, _e_text_cb_bind_text_input_manager);

   if (!text_input_mgr->global)
     {
        free(text_input_mgr);
        return EINA_FALSE;
     }

   _e_mod_text_input_shutdown_cb_add(_e_text_input_manager_destroy, text_input_mgr);

   return EINA_TRUE;
}

static void
_e_mod_text_input_shutdown(void)
{
   E_Mod_Text_Input_Shutdown_Cb *cb;

   EINA_LIST_FREE(shutdown_list, cb)
     {
        cb->func(cb->data);
        free(cb);
     }
}

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Wl_Text_Input" };

static Eina_Bool is_number_key(const char *str)
{
   if (!str) return EINA_FALSE;

   int result = atoi(str);

   if (result == 0)
     {
        if (!strcmp(str, "0"))
          return EINA_TRUE;
        else
          return EINA_FALSE;
     }
   else
     return EINA_TRUE;
}

static void
_e_mod_eeze_udev_watch_cb(const char *text, Eeze_Udev_Event event, void *data, Eeze_Udev_Watch *watch)
{
   if (event == EEZE_UDEV_EVENT_REMOVE)
     g_keyboard_connecting = EINA_FALSE;
}

static Eina_Bool
_e_mod_ecore_key_down_cb(void *data, int type, void *event)
{
   Ecore_Event_Key *ev = (Ecore_Event_Key *)event;

   if (g_keyboard_connecting == EINA_TRUE)
     return ECORE_CALLBACK_PASS_ON;

   /* process remote controller key exceptionally */
   if (((!strcmp(ev->key, "Down") ||
         !strcmp(ev->key, "KP_Down") ||
         !strcmp(ev->key, "Up") ||
         !strcmp(ev->key, "KP_Up") ||
         !strcmp(ev->key, "Right") ||
         !strcmp(ev->key, "KP_Right") ||
         !strcmp(ev->key, "Left") ||
         !strcmp(ev->key, "KP_Left")) && !ev->string) ||
       !strcmp(ev->key, "Return") ||
       !strcmp(ev->key, "Pause") ||
       !strcmp(ev->key, "NoSymbol") ||
       !strncmp(ev->key, "XF86", 4) ||
       is_number_key(ev->string))
     return ECORE_CALLBACK_PASS_ON;

   g_keyboard_connecting = EINA_TRUE;
   e_input_panel_visibility_change(EINA_FALSE);

   return ECORE_CALLBACK_PASS_ON;
}

EAPI void *
e_modapi_init(E_Module *m)
{
   E_Comp_Data *cdata = NULL;

   if (!e_comp) return NULL;

   if (!(cdata = e_comp->wl_comp_data)) return NULL;

   // FIXME: create only one input method object per seat.
   if (!_e_text_input_method_create(cdata))
     return NULL;

   if (!e_input_panel_init(cdata))
     goto err;

   if (!_e_text_input_manager_create(cdata))
     goto err;

   eeze_udev_watch_hander = eeze_udev_watch_add(EEZE_UDEV_TYPE_KEYBOARD,
                                                EEZE_UDEV_EVENT_REMOVE,
                                                _e_mod_eeze_udev_watch_cb,
                                                NULL);
   if (!eeze_udev_watch_hander)
     goto err;

   ecore_key_down_handler = ecore_event_handler_add(ECORE_EVENT_KEY_DOWN,
                                                    _e_mod_ecore_key_down_cb,
                                                    NULL);

   if (!ecore_key_down_handler)
     goto err;

   return m;
err:
   _e_mod_text_input_shutdown();
   return NULL;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   if (ecore_key_down_handler)
     {
        ecore_event_handler_del(ecore_key_down_handler);
        ecore_key_down_handler = NULL;
     }

   if (eeze_udev_watch_hander)
     {
        eeze_udev_watch_del(eeze_udev_watch_hander);
        eeze_udev_watch_hander = NULL;
     }
   _e_mod_text_input_shutdown();

   e_input_panel_shutdown(NULL);

   return 1;
}
