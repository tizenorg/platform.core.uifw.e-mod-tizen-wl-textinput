#define E_COMP_WL
#include "e.h"
#include "e_mod_main.h"
#include <input-method-server-protocol.h>

typedef struct _E_Input_Panel E_Input_Panel;
typedef struct _E_Input_Panel_Surface E_Input_Panel_Surface;

struct _E_Input_Panel
{
   struct wl_global *global;
   struct wl_resource *resource;
   Eina_List *surfaces;
};

struct _E_Input_Panel_Surface
{
   struct wl_resource *resource;

   E_Input_Panel *input_panel;
   E_Client *ec;

   Ecore_Event_Handler *rot_handler;

   Eina_Bool panel;
   Eina_Bool showing;
};

E_Input_Panel *g_input_panel = NULL;
Eina_List *handlers = NULL;

static void
_e_input_panel_surface_cb_toplevel_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *output_resource EINA_UNUSED, uint32_t position EINA_UNUSED)
{
   E_Input_Panel_Surface *ips = wl_resource_get_user_data(resource);
   E_Input_Panel *input_panel = NULL;

   if (!ips)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!(input_panel = ips->input_panel)) return;

   input_panel->surfaces = eina_list_append(input_panel->surfaces, ips);
   ips->panel = EINA_FALSE;
}

static void
_e_input_panel_surface_cb_overlay_panel_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Input_Panel_Surface *ips = wl_resource_get_user_data(resource);
   E_Input_Panel *input_panel = NULL;

   if (!ips)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!(input_panel = ips->input_panel)) return;

   input_panel->surfaces = eina_list_append(input_panel->surfaces, ips);
   ips->panel = EINA_TRUE;
}

static void
_e_input_panel_surface_cb_ready_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t state EINA_UNUSED)
{
}

static const struct wl_input_panel_surface_interface _e_input_panel_surface_implementation = {
     _e_input_panel_surface_cb_toplevel_set,
     _e_input_panel_surface_cb_overlay_panel_set,
     _e_input_panel_surface_cb_ready_set
};

static void
_e_input_panel_surface_resource_destroy(struct wl_resource *resource)
{
   E_Input_Panel_Surface *ips = wl_resource_get_user_data(resource);
   E_Input_Panel *input_panel = NULL;
   E_Client *ec = NULL;

   if (!ips)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!(input_panel = ips->input_panel)) return;

   ec = ips->ec;
   if (ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) return;

        if (ec->comp_data)
          {
             if (ec->comp_data->mapped)
               {
                  if ((ec->comp_data->shell.surface) &&
                      (ec->comp_data->shell.unmap))
                    ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
               }
             if (ec->parent)
               {
                  ec->parent->transients =
                     eina_list_remove(ec->parent->transients, ec);
               }
             ec->comp_data->shell.surface = NULL;
          }
     }

   input_panel->surfaces = eina_list_remove(input_panel->surfaces, ips);
   E_FREE_FUNC(ips->rot_handler, ecore_event_handler_del);
   free(ips);
}

static void
_e_input_panel_position_set(E_Client *ec, int w, int h)
{
   int nx, ny;
   int zx, zy, zw, zh;

   if (!ec) return;

   e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);

   /* Get the position of center bottom each angles */
   switch (ec->e.state.rot.ang.curr)
     {
        case 90:
           nx = zx + zw - w;
           ny = zy + (zh - h) / 2;
           break;
        case 180:
           nx = zx + (zw - w) / 2;
           ny = zy;
           break;
        case 270:
           nx = zx;
           ny = zy + (zh - h) / 2;
           break;
        case 0:
        default:
           nx = zx + (zw - w) / 2;
           ny = zy + zh - h;
           break;
     }

   e_client_util_move_without_frame(ec, nx, ny);
}

static void
_e_input_panel_surface_visible_update(E_Input_Panel_Surface *ips)
{
   E_Client *ec;

   if (!ips) return;

   if (!(ec = ips->ec)) return;

   if (e_object_is_del(E_OBJECT(ec))) return;

   if ((ips->showing) && (e_pixmap_usable_get(ec->pixmap)))
     {
        _e_input_panel_position_set(ec, ec->client.w, ec->client.h);

        ec->visible = EINA_TRUE;
        evas_object_geometry_set(ec->frame, ec->x, ec->y, ec->w, ec->h);
        evas_object_show(ec->frame);
        e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
     }
   else
     {
        ec->visible = EINA_FALSE;
        evas_object_hide(ec->frame);
     }
}

static void
_e_input_panel_surface_configure(struct wl_resource *resource, Evas_Coord x EINA_UNUSED, Evas_Coord y EINA_UNUSED, Evas_Coord w, Evas_Coord h)
{
   E_Input_Panel_Surface *ips;
   E_Client *ec;

   if (!(ips = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!(ec = ips->ec))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Input Panel Surface");
        return;
     }

   e_client_util_resize_without_frame(ec, w, h);

   if (ips->showing)
     _e_input_panel_surface_visible_update(ips);
}

static void
_e_input_panel_surface_map(struct wl_resource *resource)
{
   E_Input_Panel_Surface *ips;
   E_Client *ec;

   if (!(ips = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!(ec = ips->ec))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Input Panel Surface");
        return;
     }

   if (!ec->comp_data)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* NOTE: we need to set mapped, so that avoid showing evas_object and continue buffer's commit process. */
   if ((!ec->comp_data->mapped) && (e_pixmap_usable_get(ec->pixmap)))
     ec->comp_data->mapped = EINA_TRUE;
}

static void
_e_input_panel_surface_unmap(struct wl_resource *resource)
{
   E_Input_Panel_Surface *ips = NULL;
   E_Client *ec = NULL;

   if (!(ips = wl_resource_get_user_data(resource)))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!(ec = ips->ec))
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Client For Input Panel Surface");
        return;
     }

   if (!ec->comp_data)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   if (e_object_is_del(E_OBJECT(ec))) return;

   if (ec->comp_data->mapped)
     {
        ec->visible = EINA_FALSE;
        evas_object_hide(ec->frame);
        ec->comp_data->mapped = EINA_FALSE;
     }
}

static Eina_Bool
_e_input_panel_client_cb_rotation_change_end(void *data, int type, void *event)
{
   E_Client *ec;
   E_Input_Panel_Surface *ips = data;
   E_Event_Client_Rotation_Change_End *ev = event;

   ec = ev->ec;
   if (ec != ips->ec)
     goto end;

   if (ips->showing)
     _e_input_panel_position_set(ec, ec->client.w, ec->client.h);

end:
   return ECORE_CALLBACK_PASS_ON;
}


static void
_e_ips_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Client *ec;
   int w, h;

   ec = data;
   evas_object_geometry_get(obj, NULL, NULL, &w, &h);

   _e_input_panel_position_set(ec, w, h);
}

static void
_e_input_panel_cb_surface_get(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   E_Input_Panel *input_panel = wl_resource_get_user_data(resource);
   E_Client *ec = wl_resource_get_user_data(surface_resource);
   E_Input_Panel_Surface *ips = NULL;
   E_Comp_Client_Data *cdata = NULL;

   if (!input_panel)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel Surface For Surface");
        return;
     }

   if (!ec)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No E_Client Set On Surface");
        return;
     }

   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL)
     return;

   if (!(cdata = ec->comp_data))
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Data For Client");
        return;
     }

   /* check for existing shell surface */
   if (cdata->shell.surface)
     {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "Client already has shell surface");
        return;
     }

   if (!(cdata->shell.surface =
         wl_resource_create(client, &wl_input_panel_surface_interface, 1, id)))
     {
        wl_resource_post_no_memory(surface_resource);
        /* NOTE: Cleanup E_client. */
        return;
     }

   if (ec->ignored)
       e_client_unignore(ec);

   /* set input panel client properties */
   ec->borderless = EINA_TRUE;
   ec->argb = EINA_TRUE;
   ec->lock_border = EINA_TRUE;
   ec->lock_focus_in = ec->lock_focus_out = EINA_TRUE;
   ec->netwm.state.skip_taskbar = EINA_TRUE;
   ec->netwm.state.skip_pager = EINA_TRUE;
   ec->no_shape_cut = EINA_TRUE;
   ec->border_size = 0;
   ec->vkbd.vkbd = 1;
   ec->icccm.window_role = eina_stringshare_add("input_panel_surface");
   evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
   evas_object_raise(ec->frame);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE, _e_ips_cb_evas_resize, ec);

   cdata->surface = surface_resource;
   cdata->shell.configure_send = NULL;
   cdata->shell.configure = _e_input_panel_surface_configure;
   cdata->shell.ping = NULL;
   cdata->shell.map = _e_input_panel_surface_map;
   cdata->shell.unmap = _e_input_panel_surface_unmap;

   if (!(ips = E_NEW(E_Input_Panel_Surface, 1)))
     {
        wl_client_post_no_memory(client);
        return;
     }

   ips->ec = ec;
   ips->input_panel = input_panel;

   input_panel->surfaces = eina_list_append(input_panel->surfaces, ips);

   wl_resource_set_implementation(cdata->shell.surface,
                                  &_e_input_panel_surface_implementation,
                                  ips, _e_input_panel_surface_resource_destroy);

   ips->rot_handler =
      ecore_event_handler_add(E_EVENT_CLIENT_ROTATION_CHANGE_END,
                              _e_input_panel_client_cb_rotation_change_end, ips);
}


static const struct wl_input_panel_interface _e_input_panel_implementation = {
     _e_input_panel_cb_surface_get
};

static void
_e_input_panel_unbind(struct wl_resource *resource)
{
   Eina_List *l;
   E_Input_Panel_Surface *ips;
   E_Input_Panel *input_panel = wl_resource_get_user_data(resource);

   if (!input_panel)
     {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "No Input Panel For Resource");
        return;
     }

   input_panel->resource = NULL;

   l = eina_list_clone(input_panel->surfaces);
   EINA_LIST_FREE(l, ips)
     {
        E_Client *ec;
        E_Comp_Wl_Client_Data *cdata;

        if (!(ec = ips->ec)) continue;
        if (!(cdata = ec->comp_data)) continue;

        cdata->shell.surface = NULL;
     }

   E_FREE_FUNC(input_panel->surfaces, eina_list_free);
}

static void
_e_input_panel_bind(struct wl_client *client, void *data, uint32_t version EINA_UNUSED, uint32_t id)
{
   E_Input_Panel *input_panel = data;
   struct wl_resource *resource;

   if (!input_panel) return;

   resource = wl_resource_create(client, &wl_input_panel_interface, 1, id);
   if (!resource) return;

   if (input_panel->resource == NULL)
     {
        wl_resource_set_implementation(resource,
                                       &_e_input_panel_implementation,
                                       input_panel, _e_input_panel_unbind);

        input_panel->resource = resource;

        return;
     }

   wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                          "interface object already bound");
}

void
e_input_panel_visibility_change(Eina_Bool visible)
{
   E_Input_Panel_Surface *ips;
   Eina_List *l;

   if (!g_input_panel) return;

   EINA_LIST_FOREACH(g_input_panel->surfaces, l, ips)
     {
        if (!ips->ec) continue;
        ips->showing = visible;
        _e_input_panel_surface_visible_update(ips);
     }
}

Eina_Bool
e_input_panel_client_find(E_Client *ec)
{
   E_Input_Panel_Surface *ips;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (!g_input_panel) return EINA_FALSE;

   EINA_LIST_FOREACH(g_input_panel->surfaces, l, ips)
     {
        if (!ips->ec) continue;
        if (ips->ec == ec) return EINA_TRUE;
     }

   return EINA_FALSE;
}

Eina_Bool
e_input_panel_init(void)
{
   if (!e_comp_wl) return EINA_FALSE;

   if (!(g_input_panel = E_NEW(E_Input_Panel, 1)))
     {
        /* ERR("Failed to allocate space for E_Input_Panel"); */
        return EINA_FALSE;
     }

   g_input_panel->global = wl_global_create(e_comp_wl->wl.disp,
                                            &wl_input_panel_interface, 1,
                                            g_input_panel, _e_input_panel_bind);

   if (!g_input_panel->global)
     {
        free(g_input_panel);
        g_input_panel = NULL;
        /* ERR("Failed to create global of wl_input_panel"); */
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

void
e_input_panel_shutdown(void)
{
    if (g_input_panel)
      {
         if (g_input_panel->resource)
           {
              wl_resource_destroy(g_input_panel->resource);
              g_input_panel->resource = NULL;
           }

         if (g_input_panel->global)
           {
              wl_global_destroy(g_input_panel->global);
              g_input_panel->global = NULL;
           }
      }
}
