#include "e.h"
#include "e_comp_wl.h"
#include "e_comp_wl_input.h"
#include "e_comp_wl_data.h"
#include "e_surface.h"

#define E_COMP_WL_PIXMAP_CHECK \
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return

/* local variables */
static Eina_List *handlers = NULL;
static Eina_Hash *clients_win_hash = NULL;
static Ecore_Idle_Enterer *_client_idler = NULL;
static Eina_List *_idle_clients = NULL;

static void 
_e_comp_wl_surface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void 
_e_comp_wl_surface_cb_attach(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *buffer_resource, int32_t sx, int32_t sy)
{
   E_Pixmap *cp;
   E_Client *ec;
   int bw = 0, bh = 0;

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;

   if (buffer_resource)
     {
        struct wl_shm_buffer *b;

        b = wl_shm_buffer_get(buffer_resource);
        bw = wl_shm_buffer_get_width(b);
        bh = wl_shm_buffer_get_height(b);
     }

   if (ec->wl_comp_data)
     {
        ec->wl_comp_data->pending.x = sx;
        ec->wl_comp_data->pending.y = sy;
        ec->wl_comp_data->pending.w = bw;
        ec->wl_comp_data->pending.h = bh;
        ec->wl_comp_data->pending.buffer = buffer_resource;
        ec->wl_comp_data->pending.new_attach = EINA_TRUE;
     }
}

static void 
_e_comp_wl_surface_cb_damage(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Pixmap *cp;
   E_Client *ec;

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;

   if (ec->wl_comp_data)
     {
        EINA_RECTANGLE_SET(ec->wl_comp_data->damage, x, y, w, h);
     }
   else
     {
        /* FIXME: NB: This can happen with pointer surfaces */
        /* DBG("\tSurface Has No Client"); */
     }
}

static void 
_e_comp_wl_surface_cb_frame_destroy(struct wl_resource *resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->wl_comp_data) return;

   /* remove this frame callback from the list */
   ec->wl_comp_data->frames = 
     eina_list_remove(ec->wl_comp_data->frames, resource);
}

static void 
_e_comp_wl_surface_cb_frame(struct wl_client *client, struct wl_resource *resource, uint32_t callback)
{
   E_Pixmap *cp;
   E_Client *ec;
   struct wl_resource *res;

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;
   if (!ec->wl_comp_data) return;

   /* create frame callback */
   res = wl_resource_create(client, &wl_callback_interface, 1, callback);
   if (!res)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res, NULL, ec, 
                                  _e_comp_wl_surface_cb_frame_destroy);

   /* add this frame callback to the client */
   ec->wl_comp_data->frames = eina_list_prepend(ec->wl_comp_data->frames, res);
}

static void 
_e_comp_wl_surface_cb_opaque_region_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *region_resource)
{
   E_Pixmap *cp;
   E_Client *ec;

   /* DBG("E_Surface Opaque Region Set"); */

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;
   if (!ec->wl_comp_data) return;

   if (region_resource)
     {
        Eina_Rectangle *opq;

        if (!(opq = wl_resource_get_user_data(region_resource))) return;

        EINA_RECTANGLE_SET(ec->wl_comp_data->opaque, 
                           opq->x, opq->y, opq->w, opq->h);
     }
   else
     {
        EINA_RECTANGLE_SET(ec->wl_comp_data->opaque, 0, 0, 0, 0);
//                           0, 0, ec->client.w, ec->client.h);
     }
}

static void 
_e_comp_wl_surface_cb_input_region_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *region_resource)
{
   E_Pixmap *cp;
   E_Client *ec;

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;
   if (!ec->wl_comp_data) return;

   if (region_resource)
     {
        Eina_Rectangle *input;

        if (!(input = wl_resource_get_user_data(region_resource))) return;

        DBG("\tInput Area: %d %d %d %d", input->x, input->y, 
            input->w, input->h);

        EINA_RECTANGLE_SET(ec->wl_comp_data->input, 
                           input->x, input->y, input->w, input->h);
     }
   /* else */
   /*   { */
   /*      EINA_RECTANGLE_SET(ec->wl_comp_data->input, 0, 0,  */
   /*                         ec->client.w, ec->client.h); */
   /*   } */
}

static void 
_e_comp_wl_surface_cb_commit(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Pixmap *cp;
   E_Client *ec;

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;

   if (!ec->wl_comp_data) return;

   if (ec->wl_comp_data->pending.new_attach)
     {
        e_pixmap_resource_set(cp, ec->wl_comp_data->pending.buffer);
        e_pixmap_usable_set(cp, (ec->wl_comp_data->pending.buffer != NULL));
     }

   e_pixmap_dirty(cp);
   e_pixmap_refresh(cp);

   if ((ec->wl_comp_data->shell.surface) && 
       (ec->wl_comp_data->shell.configure))
     {
        if (ec->wl_comp_data->pending.buffer)
          {
             if ((ec->client.w != ec->wl_comp_data->pending.w) || 
                 (ec->client.h != ec->wl_comp_data->pending.h))
               ec->wl_comp_data->shell.configure(ec->wl_comp_data->shell.surface, 
                                                 ec->client.x, ec->client.y, 
                                                 ec->wl_comp_data->pending.w, 
                                                 ec->wl_comp_data->pending.h);
          }
     }

   if (!ec->wl_comp_data->pending.buffer)
     {
        if (ec->wl_comp_data->mapped)
          {
             if ((ec->wl_comp_data->shell.surface) && 
                 (ec->wl_comp_data->shell.unmap))
               ec->wl_comp_data->shell.unmap(ec->wl_comp_data->shell.surface);
          }
     }
   else
     {
        if (!ec->wl_comp_data->mapped)
          {
             if ((ec->wl_comp_data->shell.surface) && 
                 (ec->wl_comp_data->shell.map))
               ec->wl_comp_data->shell.map(ec->wl_comp_data->shell.surface);
          }
     }

#ifndef HAVE_WAYLAND_ONLY
   if (ec->frame)
     {
        Eina_Rectangle *rect;

        rect = ec->wl_comp_data->input;
        e_comp_object_input_area_set(ec->frame, 
                                     rect->x, rect->y, rect->w, rect->h);
     }
#endif

   /* handle surface opaque region */
   ec->wl_comp_data->shape = ec->wl_comp_data->input;

   /* handle surface damages */
   if ((!ec->comp->nocomp) && (ec->frame))
     {
        Eina_Rectangle *dmg;

        dmg = ec->wl_comp_data->damage;
        /* DBG("\tDmg: %d %d %d %d", dmg->x, dmg->y, dmg->w, dmg->h); */
        e_comp_object_damage(ec->frame, dmg->x, dmg->y, dmg->w, dmg->h);
     }

   /* handle surface input region */
   ec->shape_input_rects_num = 1;
   ec->shape_input_rects = ec->wl_comp_data->input;
   ec->changes.shape_input = EINA_TRUE;
   EC_CHANGED(ec);

   ec->wl_comp_data->pending.x = 0;
   ec->wl_comp_data->pending.y = 0;
   ec->wl_comp_data->pending.w = 0;
   ec->wl_comp_data->pending.h = 0;
   ec->wl_comp_data->pending.buffer = NULL;
   ec->wl_comp_data->pending.new_attach = EINA_FALSE;
}

static void 
_e_comp_wl_surface_cb_buffer_transform_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, int32_t transform EINA_UNUSED)
{
   DBG("Surface Buffer Transform");
}

static void 
_e_comp_wl_surface_cb_buffer_scale_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, int32_t scale EINA_UNUSED)
{
   DBG("Surface Buffer Scale");
}

static const struct wl_surface_interface _e_comp_wl_surface_interface = 
{
   _e_comp_wl_surface_cb_destroy,
   _e_comp_wl_surface_cb_attach,
   _e_comp_wl_surface_cb_damage,
   _e_comp_wl_surface_cb_frame,
   _e_comp_wl_surface_cb_opaque_region_set,
   _e_comp_wl_surface_cb_input_region_set,
   _e_comp_wl_surface_cb_commit,
   _e_comp_wl_surface_cb_buffer_transform_set,
   _e_comp_wl_surface_cb_buffer_scale_set
};

static void 
_e_comp_wl_comp_cb_surface_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Comp *comp;
   E_Pixmap *cp;
   struct wl_resource *res;
   uint64_t wid;
   pid_t pid;

   /* DBG("COMP_WL: Create Surface: %d", id); */

   if (!(comp = wl_resource_get_user_data(resource))) return;

   res = 
     e_comp_wl_surface_create(client, wl_resource_get_version(resource), id);
   if (!res)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   /* get the client pid and generate a pixmap id */
   wl_client_get_credentials(client, &pid, NULL, NULL);
   wid = e_comp_wl_id_get(pid, id);

   /* see if we already have a pixmap for this surface */
   if (!(cp = e_pixmap_find(E_PIXMAP_TYPE_WL, wid)))
     {
        /* try to create a new pixmap for this surface */
        if (!(cp = e_pixmap_new(E_PIXMAP_TYPE_WL, wid)))
          {
             wl_resource_destroy(res);
             wl_resource_post_no_memory(resource);
             return;
          }
     }

   /* set reference to pixmap so we can fetch it later */
   wl_resource_set_user_data(res, cp);
}

static void 
_e_comp_wl_region_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void 
_e_comp_wl_region_cb_add(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   Eina_Rectangle *rect;

   if ((rect = wl_resource_get_user_data(resource)))
     EINA_RECTANGLE_SET(rect, x, y, w, h);
}

static void 
_e_comp_wl_region_cb_subtract(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, int32_t x, int32_t y, int32_t w, int32_t h)
{
   DBG("Comp Region Subtract: %d %d %d %d", x, y, w, h);

   /* Eina_Rectangle *rect; */

   /* if ((rect = wl_resource_get_user_data(resource))) */
   /*   { */
   /*      eina_rectangle_subtract(rect); */
   /*   } */
}

static const struct wl_region_interface _e_region_interface = 
{
   _e_comp_wl_region_cb_destroy,
   _e_comp_wl_region_cb_add,
   _e_comp_wl_region_cb_subtract
};

static void 
_e_comp_wl_comp_cb_region_destroy(struct wl_resource *resource)
{
   Eina_Rectangle *rect;

   if ((rect = wl_resource_get_user_data(resource)))
     eina_rectangle_free(rect);
}

static void 
_e_comp_wl_comp_cb_region_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   Eina_Rectangle *rect;
   struct wl_resource *res;

   /* try to create new rectangle */
   if (!(rect = eina_rectangle_new(0, 0, 0, 0)))
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   /* try to create new wayland resource */
   if (!(res = wl_resource_create(client, &wl_region_interface, 1, id)))
     {
        eina_rectangle_free(rect);
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res, &_e_region_interface, rect, 
                                  _e_comp_wl_comp_cb_region_destroy);
}

static void 
_e_comp_wl_subcomp_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void 
_e_comp_wl_subcomp_cb_subsurface_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, uint32_t id EINA_UNUSED, struct wl_resource *surface_resource EINA_UNUSED, struct wl_resource *parent_resource EINA_UNUSED)
{
   /* NB: Needs New Resource */
#warning TODO Need to subcomp subsurface
}

static const struct wl_compositor_interface _e_comp_interface = 
{
   _e_comp_wl_comp_cb_surface_create,
   _e_comp_wl_comp_cb_region_create
};

static const struct wl_subcompositor_interface _e_subcomp_interface = 
{
   _e_comp_wl_subcomp_cb_destroy,
   _e_comp_wl_subcomp_cb_subsurface_get
};

static void 
_e_comp_wl_cb_bind_compositor(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp *comp;
   struct wl_resource *res;

   if (!(comp = data)) return;

   res = 
     wl_resource_create(client, &wl_compositor_interface, 
                        MIN(version, 3), id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_comp_interface, comp, NULL);
}

static void 
_e_comp_wl_cb_bind_subcompositor(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp *comp;
   struct wl_resource *res;

   if (!(comp = data)) return;

   res = 
     wl_resource_create(client, &wl_subcompositor_interface, 
                        MIN(version, 1), id);
   if (!res)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_subcomp_interface, comp, NULL);
}

static void 
_e_comp_wl_cb_render_post(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, void *event EINA_UNUSED)
{
   Eina_Iterator *itr;
   E_Client *ec;

   if (!(itr = eina_hash_iterator_data_new(clients_win_hash))) return;

   EINA_ITERATOR_FOREACH(itr, ec)
     {
        struct wl_resource *cb;

        if (!ec->comp_data) continue;
        EINA_LIST_FREE(ec->comp_data->frames, cb)
          {
             wl_callback_send_done(cb, ecore_loop_time_get());
             wl_resource_destroy(cb);
          }
     }

   eina_iterator_free(itr);
}

static void 
_e_comp_wl_cb_del(E_Comp *comp)
{
   E_Comp_Data *cdata;

   cdata = comp->comp_data;

   e_comp_wl_data_manager_shutdown(cdata);
   e_comp_wl_input_shutdown(cdata);

   /* remove render_post callback */
   evas_event_callback_del_full(comp->evas, EVAS_CALLBACK_RENDER_POST, 
                                _e_comp_wl_cb_render_post, NULL);

   /* delete idler to flush clients */
   if (cdata->idler) ecore_idler_del(cdata->idler);

   /* delete fd handler to listen for wayland main loop events */
   if (cdata->fd_hdlr) ecore_main_fd_handler_del(cdata->fd_hdlr);

   /* delete the wayland display */
   if (cdata->wl.disp) wl_display_destroy(cdata->wl.disp);

   free(cdata);
}

static Eina_Bool 
_e_comp_wl_cb_read(void *data, Ecore_Fd_Handler *hdl EINA_UNUSED)
{
   E_Comp_Data *cdata;

   if (!(cdata = data)) return ECORE_CALLBACK_RENEW;
   if (!cdata->wl.disp) return ECORE_CALLBACK_RENEW;

   /* DBG("Compositor Read"); */

   /* dispatch any pending main loop events */
   wl_event_loop_dispatch(cdata->wl.loop, 0);

   /* flush any pending client events */
   wl_display_flush_clients(cdata->wl.disp);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool 
_e_comp_wl_cb_idle(void *data)
{
   E_Comp_Data *cdata;

   if (!(cdata = data)) return ECORE_CALLBACK_RENEW;
   if (!cdata->wl.disp) return ECORE_CALLBACK_RENEW;

   /* flush any pending client events */
   wl_display_flush_clients(cdata->wl.disp);

   /* dispatch any pending main loop events */
   wl_event_loop_dispatch(cdata->wl.loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool 
_e_comp_wl_cb_module_idle(void *data)
{
   E_Module *mod = NULL;
   E_Comp_Data *cdata;

   if (!(cdata = data)) return ECORE_CALLBACK_RENEW;

   if (e_module_loading_get()) return ECORE_CALLBACK_RENEW;

   /* FIXME: make which shell to load configurable */
   if (!(mod = e_module_find("wl_desktop_shell")))
     mod = e_module_new("wl_desktop_shell");

   if (mod)
     {
        e_module_enable(mod);

        /* dispatch any pending main loop events */
        wl_event_loop_dispatch(cdata->wl.loop, 0);

        return ECORE_CALLBACK_CANCEL;
     }

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool 
_e_comp_wl_cb_first_draw(void *data)
{
   E_Client *ec;

   if (!(ec = data)) return EINA_TRUE;
   ec->comp_data->first_draw_tmr = NULL;
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   return EINA_FALSE;
}

static Eina_Bool 
_e_comp_wl_compositor_create(void)
{
   E_Comp *comp;
   E_Comp_Data *cdata;
   char buff[PATH_MAX];
   int fd = 0;

   /* get the current compositor */
   if (!(comp = e_comp_get(NULL))) return EINA_FALSE;

   /* check compositor type and make sure it's Wayland */
   /* if (comp->comp_type != E_PIXMAP_TYPE_WL) return EINA_FALSE; */

   E_OBJECT_DEL_SET(comp, _e_comp_wl_cb_del);
   cdata = E_NEW(E_Comp_Data, 1);
   comp->comp_data = cdata;

   /* setup wayland display environment variable */
   snprintf(buff, sizeof(buff), "%s/wayland-0", e_ipc_socket);
   e_env_set("WAYLAND_DISPLAY", buff);

   /* try to create wayland display */
   if (!(cdata->wl.disp = wl_display_create()))
     {
        ERR("Could not create a Wayland Display: %m");
        goto disp_err;
     }

   if (wl_display_add_socket(cdata->wl.disp, buff))
     {
        ERR("Could not create a Wayland Display: %m");
        goto disp_err;
     }

   /* setup wayland compositor signals */
   /* NB: So far, we don't need ANY of these... */
   /* wl_signal_init(&cdata->signals.destroy); */
   /* wl_signal_init(&cdata->signals.activate); */
   /* wl_signal_init(&cdata->signals.transform); */
   /* wl_signal_init(&cdata->signals.kill); */
   /* wl_signal_init(&cdata->signals.idle); */
   /* wl_signal_init(&cdata->signals.wake); */
   /* wl_signal_init(&cdata->signals.session); */
   /* wl_signal_init(&cdata->signals.seat.created); */
   /* wl_signal_init(&cdata->signals.seat.destroyed); */
   /* wl_signal_init(&cdata->signals.seat.moved); */
   /* wl_signal_init(&cdata->signals.output.created); */
   /* wl_signal_init(&cdata->signals.output.destroyed); */
   /* wl_signal_init(&cdata->signals.output.moved); */

   /* try to add compositor to wayland display globals */
   if (!wl_global_create(cdata->wl.disp, &wl_compositor_interface, 3, 
                         comp, _e_comp_wl_cb_bind_compositor))
     {
        ERR("Could not add compositor to globals: %m");
        goto disp_err;
     }

   /* try to add subcompositor to wayland display globals */
   if (!wl_global_create(cdata->wl.disp, &wl_subcompositor_interface, 1, 
                         comp, _e_comp_wl_cb_bind_subcompositor))
     {
        ERR("Could not add compositor to globals: %m");
        goto disp_err;
     }

   /* try to init data manager */
   if (!e_comp_wl_data_manager_init(cdata))
     {
        ERR("Could not initialize data manager");
        goto disp_err;
     }

   /* try to init input (keyboard & pointer) */
   if (!e_comp_wl_input_init(cdata))
     {
        ERR("Could not initialize input");
        goto disp_err;
     }

   /* TODO: init text backend */

   /* initialize shm mechanism */
   wl_display_init_shm(cdata->wl.disp);

   /* check for gl rendering */
   if ((e_comp_gl_get()) && 
       (e_comp_config_get()->engine == E_COMP_ENGINE_GL))
     {
        /* TODO: setup gl ? */
     }

   /* get the wayland display's event loop */
   cdata->wl.loop= wl_display_get_event_loop(cdata->wl.disp);

   /* get the file descriptor of the main loop */
   fd = wl_event_loop_get_fd(cdata->wl.loop);

   /* add an fd handler to listen for wayland main loop events */
   cdata->fd_hdlr = 
     ecore_main_fd_handler_add(fd, ECORE_FD_READ | ECORE_FD_WRITE, 
                               _e_comp_wl_cb_read, cdata, NULL, NULL);

   /* add an idler to flush clients */
   cdata->idler = ecore_idle_enterer_add(_e_comp_wl_cb_idle, cdata);

   /* setup module idler to load shell module */
   ecore_idler_add(_e_comp_wl_cb_module_idle, cdata);

   /* add a render post callback so we can send frame_done to the surface */
   evas_event_callback_add(comp->evas, EVAS_CALLBACK_RENDER_POST, 
                           _e_comp_wl_cb_render_post, NULL);

   return EINA_TRUE;

disp_err:
   e_env_unset("WAYLAND_DISPLAY");
   return EINA_FALSE;
}

static Eina_Bool 
_e_comp_wl_client_idler(void *data EINA_UNUSED)
{
   E_Client *ec;
   E_Comp *comp;
   const Eina_List *l;

   EINA_LIST_FREE(_idle_clients, ec)
     {
        if ((e_object_is_del(E_OBJECT(ec))) || (!ec->comp_data)) continue;

        ec->post_move = 0;
        ec->post_resize = 0;
     }

   EINA_LIST_FOREACH(e_comp_list(), l, comp)
     {
        if ((comp->comp_data->restack) && (!comp->new_clients))
          {
             e_hints_client_stacking_set();
             comp->comp_data->restack = EINA_FALSE;
          }
     }

   _client_idler = NULL;
   return EINA_FALSE;
}

static void 
_e_comp_wl_client_idler_add(E_Client *ec)
{
   if (!_client_idler) 
     _client_idler = ecore_idle_enterer_add(_e_comp_wl_client_idler, NULL);

   if (!ec) CRI("ACK!");

   if (!eina_list_data_find(_idle_clients, ec))
     _idle_clients = eina_list_append(_idle_clients, ec);
}

static void 
_e_comp_wl_evas_cb_show(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *tmp;
   Eina_List *l;

   if (!(ec = data)) return;

   if (!ec->override) 
     e_hints_window_visible_set(ec);

   if (ec->comp_data->frame_update)
     ec->comp_data->frame_update = EINA_FALSE;

   EINA_LIST_FOREACH(ec->e.state.video_child, l, tmp)
     evas_object_show(tmp->frame);
}

static void 
_e_comp_wl_evas_cb_hide(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *tmp;
   Eina_List *l;

   if (!(ec = data)) return;

   EINA_LIST_FOREACH(ec->e.state.video_child, l, tmp)
     evas_object_hide(tmp->frame);
}

static void 
_e_comp_wl_evas_cb_mouse_in(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_In *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;

   ev = event;
   if (!(ec = data)) return;
   if (!ec->comp_data) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_enter(res, serial, ec->comp_data->surface, 
                              wl_fixed_from_int(ev->canvas.x), 
                              wl_fixed_from_int(ev->canvas.y));
     }
}

static void 
_e_comp_wl_evas_cb_mouse_out(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;

   if (!(ec = data)) return;
   if (!ec->comp_data) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_leave(res, serial, ec->comp_data->surface);
     }
}

static void 
_e_comp_wl_evas_cb_mouse_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Down *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial, btn;

   ev = event;
   if (!(ec = data)) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_client_util_ignored_get(ec)) return;

   switch (ev->button)
     {
      case 1:
        btn = BTN_LEFT;
        break;
      case 2:
        btn = BTN_MIDDLE;
        break;
      case 3:
        btn = BTN_RIGHT;
        break;
      default:
        btn = ev->button;
        break;
     }

   ec->comp->comp_data->ptr.button = btn;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_button(res, serial, ev->timestamp, btn, 
                               WL_POINTER_BUTTON_STATE_PRESSED);
     }
}

static void 
_e_comp_wl_evas_cb_mouse_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Up *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial, btn;

   ev = event;
   if (!(ec = data)) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_client_util_ignored_get(ec)) return;

   switch (ev->button)
     {
      case 1:
        btn = BTN_LEFT;
        break;
      case 2:
        btn = BTN_MIDDLE;
        break;
      case 3:
        btn = BTN_RIGHT;
        break;
      default:
        btn = ev->button;
        break;
     }

   ec->comp->comp_data->resize.resource = NULL;
   ec->comp->comp_data->ptr.button = btn;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_button(res, serial, ev->timestamp, btn, 
                               WL_POINTER_BUTTON_STATE_RELEASED);
     }
}

static void 
_e_comp_wl_evas_cb_mouse_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Move *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;

   ev = event;
   if (!(ec = data)) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_client_util_ignored_get(ec)) return;

   ec->comp->comp_data->ptr.x = 
     wl_fixed_from_int(ev->cur.canvas.x - ec->client.x);
   ec->comp->comp_data->ptr.y = 
     wl_fixed_from_int(ev->cur.canvas.y - ec->client.y);

   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(ec->comp->comp_data->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_motion(res, ev->timestamp, 
                               ec->comp->comp_data->ptr.x, 
                               ec->comp->comp_data->ptr.y);
     }
}

static void 
_e_comp_wl_evas_cb_key_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   Evas_Event_Key_Down *ev;
   uint32_t serial, *end, *k, keycode;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;

   ev = event;
   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->focused) return;

   keycode = (ev->keycode - 8);
   cdata = ec->comp->comp_data;

   end = (uint32_t *)cdata->kbd.keys.data + cdata->kbd.keys.size;

   /* ignore server-generated key repeats */
   for (k = cdata->kbd.keys.data; k < end; k++)
     if (*k == keycode) return;

   cdata->kbd.keys.size = end - (uint32_t *)cdata->kbd.keys.data;
   k = wl_array_add(&cdata->kbd.keys, sizeof(*k));
   *k = keycode;

   /* update modifier state */
   e_comp_wl_input_keyboard_state_update(cdata, keycode, EINA_TRUE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->comp_data->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        wl_keyboard_send_key(res, serial, ev->timestamp, 
                             keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
     }
}

static void 
_e_comp_wl_evas_cb_key_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   E_Comp_Data *cdata;
   Evas_Event_Key_Up *ev;
   uint32_t serial, *end, *k, keycode;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;

   ev = event;
   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->focused) return;

   keycode = (ev->keycode - 8);
   cdata = ec->comp->comp_data;

   end = (uint32_t *)cdata->kbd.keys.data + cdata->kbd.keys.size;
   for (k = cdata->kbd.keys.data; k < end; k++)
     if (*k == keycode) *k = *--end;

   cdata->kbd.keys.size = end - (uint32_t *)cdata->kbd.keys.data;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(cdata->wl.disp);
   EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        wl_keyboard_send_key(res, serial, ev->timestamp, 
                             keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
     }

   /* update modifier state */
   e_comp_wl_input_keyboard_state_update(cdata, keycode, EINA_FALSE);
}

static void 
_e_comp_wl_evas_cb_focus_in(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *focused;
   E_Comp_Data *cdata;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial, *k;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* block refocus attempts on iconic clients
    * these result from iconifying a client during a grab */
   if (ec->iconic) return;

   /* block spurious focus events
    * not sure if correct, but seems necessary to use pointer focus... */
   focused = e_client_focused_get();
   if (focused && (ec != focused)) return;

   cdata = ec->comp->comp_data;

   /* TODO: priority raise */

   /* update modifier state */
   wl_array_for_each(k, &cdata->kbd.keys)
     e_comp_wl_input_keyboard_state_update(cdata, *k, EINA_TRUE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(cdata->wl.disp);
   EINA_LIST_FOREACH(cdata->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        wl_keyboard_send_enter(res, serial, ec->comp_data->surface, 
                               &cdata->kbd.keys);
     }
}

static void 
_e_comp_wl_evas_cb_focus_out(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial, *k;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* TODO: priority normal */

   /* update modifier state */
   wl_array_for_each(k, &ec->comp->comp_data->kbd.keys)
     e_comp_wl_input_keyboard_state_update(ec->comp->comp_data, *k, EINA_FALSE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(ec->comp->comp_data->wl.disp);
   EINA_LIST_FOREACH(ec->comp->comp_data->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        wl_keyboard_send_leave(res, serial, ec->comp_data->surface);
     }
}

static void 
_e_comp_wl_evas_cb_resize(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   E_Comp_Data *cdata;

   if (!(ec = data)) return;
   if ((ec->shading) || (ec->shaded)) return;
   if (!e_pixmap_size_changed(ec->pixmap, ec->client.w, ec->client.h))
     return;

   /* DBG("COMP_WL: Evas Resize: %d %d", ec->client.w, ec->client.h); */

   cdata = ec->comp->comp_data;

   /* if ((ec->changes.pos) || (ec->changes.size)) */
     {
        if ((ec->comp_data) && (ec->comp_data->shell.configure_send))
          ec->comp_data->shell.configure_send(ec->comp_data->shell.surface, 
                                              cdata->resize.edges,
                                              ec->client.w, ec->client.h);
     }

   ec->post_resize = EINA_TRUE;
   /* e_pixmap_dirty(ec->pixmap); */
   /* e_comp_object_render_update_del(ec->frame); */
   _e_comp_wl_client_idler_add(ec);
}

static void 
_e_comp_wl_evas_cb_frame_recalc(void *data, Evas_Object *obj, void *event)
{
   E_Client *ec;
   E_Comp_Object_Frame *fr;

   fr = event;
   if (!(ec = data)) return;
   if (!ec->comp_data) return;
   WRN("COMP_WL Frame Recalc: %d %d %d %d", fr->l, fr->r, fr->t, fr->b);
   if (evas_object_visible_get(obj))
     ec->comp_data->frame_update = EINA_FALSE;
   else
     ec->comp_data->frame_update = EINA_TRUE;
   ec->post_move = ec->post_resize = EINA_TRUE;
   _e_comp_wl_client_idler_add(ec);
}

/* static void  */
/* _e_comp_wl_evas_cb_comp_hidden(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED) */
/* { */
/*    E_Client *ec; */

/*    if (!(ec = data)) return; */
/* #warning FIXME Implement Evas Comp Hidden */
/* } */

static void 
_e_comp_wl_evas_cb_delete_request(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   DBG("COMP_WL: Evas Del Request");

   if (!(ec = data)) return;
   if (ec->netwm.ping) e_client_ping(ec);

   /* if (ec->comp_data->shell.surface) */
   /*   wl_resource_destroy(ec->comp_data->shell.surface); */


   /* FIXME !!!
    * 
    * This is a HUGE problem for internal windows...
    * 
    * IF we delete the client here, then we cannot reopen some internal 
    * dialogs (configure, etc, etc) ...
    * 
    * BUT, if we don't handle delete_request Somehow, then the close button on 
    * the frame does Nothing
    * 
    */


   /* e_comp_ignore_win_del(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ec->pixmap)); */
   /* if (ec->comp_data) */
   /*   { */
   /*      if (ec->comp_data->reparented) */
   /*        e_client_comp_hidden_set(ec, EINA_TRUE); */
   /*   } */

   /* evas_object_pass_events_set(ec->frame, EINA_TRUE); */
   /* if (ec->visible) evas_object_hide(ec->frame); */
   /* e_object_del(E_OBJECT(ec)); */

   /* TODO: Delete request send ?? */
#warning TODO Need to implement delete request ?
}

static void 
_e_comp_wl_evas_cb_kill_request(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (ec->netwm.ping) e_client_ping(ec);

   e_comp_ignore_win_del(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ec->pixmap));
   if (ec->comp_data)
     {
        if (ec->comp_data->reparented)
          e_client_comp_hidden_set(ec, EINA_TRUE);
        evas_object_pass_events_set(ec->frame, EINA_TRUE);
        evas_object_hide(ec->frame);
        e_object_del(E_OBJECT(ec));
     }
}

static void 
_e_comp_wl_evas_cb_ping(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (!ec->comp_data) return;

   if (ec->comp_data->shell.ping)
     {
        if (ec->comp_data->shell.surface)
          ec->comp_data->shell.ping(ec->comp_data->shell.surface);
     }
}

static void 
_e_comp_wl_evas_cb_color_set(void *data, Evas_Object *obj, void *event EINA_UNUSED)
{
   E_Client *ec;
   int a = 0;

   if (!(ec = data)) return;
   if (!ec->comp_data) return;
   evas_object_color_get(obj, NULL, NULL, NULL, &a);
   if (ec->netwm.opacity == a) return;
   ec->netwm.opacity_changed = EINA_TRUE;
   _e_comp_wl_client_idler_add(ec);
}

static void 
_e_comp_wl_client_evas_init(E_Client *ec)
{
   if (ec->comp_data->evas_init) return;
   ec->comp_data->evas_init = EINA_TRUE;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW, 
                                  _e_comp_wl_evas_cb_show, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE, 
                                  _e_comp_wl_evas_cb_hide, ec);

   /* we need to hook evas mouse events for wayland clients */
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_IN, 
                                  _e_comp_wl_evas_cb_mouse_in, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_OUT, 
                                  _e_comp_wl_evas_cb_mouse_out, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_DOWN, 
                                  _e_comp_wl_evas_cb_mouse_down, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_UP, 
                                  _e_comp_wl_evas_cb_mouse_up, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOUSE_MOVE, 
                                  _e_comp_wl_evas_cb_mouse_move, ec);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_KEY_DOWN, 
                                  _e_comp_wl_evas_cb_key_down, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_KEY_UP, 
                                  _e_comp_wl_evas_cb_key_up, ec);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_FOCUS_IN, 
                                  _e_comp_wl_evas_cb_focus_in, ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_FOCUS_OUT, 
                                  _e_comp_wl_evas_cb_focus_out, ec);

   if (!ec->override)
     {
        evas_object_smart_callback_add(ec->frame, "client_resize", 
                                       _e_comp_wl_evas_cb_resize, ec);
     }

   evas_object_smart_callback_add(ec->frame, "frame_recalc_done", 
                                  _e_comp_wl_evas_cb_frame_recalc, ec);
   evas_object_smart_callback_add(ec->frame, "delete_request", 
                                  _e_comp_wl_evas_cb_delete_request, ec);
   evas_object_smart_callback_add(ec->frame, "kill_request", 
                                  _e_comp_wl_evas_cb_kill_request, ec);
   evas_object_smart_callback_add(ec->frame, "ping", 
                                  _e_comp_wl_evas_cb_ping, ec);
   evas_object_smart_callback_add(ec->frame, "color_set", 
                                  _e_comp_wl_evas_cb_color_set, ec);

   /* TODO: these will need to send_configure */
   /* evas_object_smart_callback_add(ec->frame, "fullscreen_zoom",  */
   /*                                _e_comp_wl_evas_cb_resize, ec); */
   /* evas_object_smart_callback_add(ec->frame, "unfullscreen_zoom",  */
   /*                                _e_comp_wl_evas_cb_resize, ec); */
}

static Eina_Bool 
_e_comp_wl_client_new_helper(E_Client *ec)
{
   if ((!e_client_util_ignored_get(ec)) && 
       (!ec->internal) && (!ec->internal_ecore_evas))
     {
        ec->comp_data->need_reparent = EINA_TRUE;
        EC_CHANGED(ec);
        ec->take_focus = !starting;
     }

   ec->new_client ^= ec->override;

   if (e_pixmap_size_changed(ec->pixmap, ec->client.w, ec->client.h))
     {
        ec->changes.size = EINA_TRUE;
        EC_CHANGED(ec);
     }

   return EINA_TRUE;
}

static Eina_Bool 
_e_comp_wl_client_shape_check(E_Client *ec)
{
   /* check for empty shape */
   if (eina_rectangle_is_empty(ec->comp_data->shape)) 
     {
        ec->shape_rects = NULL;
        ec->shape_rects_num = 0;
     }
   else
     {
        ec->shape_rects = ec->comp_data->shape;
        ec->shape_rects_num = 1;
     }

   ec->shape_changed = EINA_TRUE;
   e_comp_shape_queue(ec->comp);
   return EINA_TRUE;
}

static Eina_Bool 
_e_comp_wl_cb_comp_object_add(void *data EINA_UNUSED, int type EINA_UNUSED, E_Event_Comp_Object *ev)
{
   E_Client *ec;

   ec = e_comp_object_client_get(ev->comp_object);

   /* NB: Don't check re_manage here as we need evas events for mouse */
   if ((!ec) || (e_object_is_del(E_OBJECT(ec))))
     return ECORE_CALLBACK_RENEW;

   _e_comp_wl_client_evas_init(ec);

   return ECORE_CALLBACK_RENEW;
}

/* static Eina_Bool  */
/* _e_comp_wl_cb_client_zone_set(void *data EINA_UNUSED, int type EINA_UNUSED, void *event) */
/* { */
/*    E_Event_Client *ev; */
/*    E_Client *ec; */

/*    DBG("CLIENT ZONE SET !!!"); */

/*    ev = event; */
/*    if (!(ec = ev->ec)) return ECORE_CALLBACK_RENEW; */
/*    if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_RENEW; */
/*    E_COMP_WL_PIXMAP_CHECK ECORE_CALLBACK_RENEW; */

/*    DBG("\tClient Zone: %d", (ec->zone != NULL)); */

/*    return ECORE_CALLBACK_RENEW; */
/* } */

static Eina_Bool 
_e_comp_wl_cb_client_prop(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client_Property *ev;

   ev = event;
   if (!(ev->property & E_CLIENT_PROPERTY_ICON)) return ECORE_CALLBACK_RENEW;
   if (e_pixmap_type_get(ev->ec->pixmap) != E_PIXMAP_TYPE_WL) 
     return ECORE_CALLBACK_RENEW;

   if (ev->ec->desktop)
     {
        if (!ev->ec->exe_inst) e_exec_phony(ev->ec);
     }

   return ECORE_CALLBACK_RENEW;
}

static void 
_e_comp_wl_cb_hook_client_move_end(void *data EINA_UNUSED, E_Client *ec)
{
   E_COMP_WL_PIXMAP_CHECK;

   /* DBG("COMP_WL HOOK CLIENT MOVE END !!"); */

   /* unset pointer */
   /* e_pointer_type_pop(e_comp_get(ec)->pointer, ec, "move"); */

   /* ec->post_move = EINA_TRUE; */
   /* _e_comp_wl_client_idler_add(ec); */
}

static void 
_e_comp_wl_cb_hook_client_del(void *data EINA_UNUSED, E_Client *ec)
{
   uint64_t win;

   E_COMP_WL_PIXMAP_CHECK;

   DBG("COMP_WL: Hook Client Del");

   if ((!ec->already_unparented) && (ec->comp_data->reparented))
     {
        /* TODO: focus setdown */
#warning TODO Need to implement focus setdown
     }

   ec->already_unparented = EINA_TRUE;
   win = e_pixmap_window_get(ec->pixmap);
   eina_hash_del_by_key(clients_win_hash, &win);

   if (ec->comp_data->input)
     eina_rectangle_free(ec->comp_data->input);
   if (ec->comp_data->opaque)
     eina_rectangle_free(ec->comp_data->opaque);
   if (ec->comp_data->shape)
     eina_rectangle_free(ec->comp_data->shape);
   if (ec->comp_data->damage)
     eina_rectangle_free(ec->comp_data->damage);

   if (ec->comp_data->reparented)
     {
        win = e_client_util_pwin_get(ec);
        eina_hash_del_by_key(clients_win_hash, &win);
        e_pixmap_parent_window_set(ec->pixmap, 0);
     }

   if ((ec->parent) && (ec->parent->modal == ec))
     {
        ec->parent->lock_close = EINA_FALSE;
        ec->parent->modal = NULL;
     }

   E_FREE_FUNC(ec->comp_data->first_draw_tmr, ecore_timer_del);

   E_FREE(ec->comp_data);
   ec->comp_data = NULL;

   /* TODO: comp focus check */
}

static void 
_e_comp_wl_cb_hook_client_new(void *data EINA_UNUSED, E_Client *ec)
{
   uint64_t win;

   E_COMP_WL_PIXMAP_CHECK;

   DBG("COMP_WL: Client New: %d", ec->internal);

   win = e_pixmap_window_get(ec->pixmap);
   ec->ignored = e_comp_ignore_win_find(win);

   /* NB: could not find a better place todo this, BUT for internal windows, 
    * we need to set delete_request else the close buttons on the frames do 
    * basically nothing */
   if (ec->internal) ec->icccm.delete_request = EINA_TRUE;

   ec->comp_data = E_NEW(E_Comp_Client_Data, 1);
   ec->comp_data->input = eina_rectangle_new(0, 0, 0, 0);
   ec->comp_data->opaque = eina_rectangle_new(0, 0, 0, 0);
   ec->comp_data->shape = eina_rectangle_new(0, 0, 0, 0);
   ec->comp_data->damage = eina_rectangle_new(0, 0, 0, 0);

   ec->comp_data->mapped = EINA_FALSE;
   ec->comp_data->set_win_type = EINA_TRUE;
   ec->netwm.type = E_WINDOW_TYPE_UNKNOWN;
   /* ec->shaped = EINA_TRUE; */
   /* ec->shaped_input = EINA_TRUE; */
   ec->changes.shape = EINA_TRUE;
   ec->changes.shape_input = EINA_TRUE;

   if (!_e_comp_wl_client_new_helper(ec)) return;
   ec->comp_data->first_damage = ((ec->internal) || (ec->override));

   eina_hash_add(clients_win_hash, &win, ec);
   e_hints_client_list_set();

   ec->comp_data->first_draw_tmr = 
     ecore_timer_add(e_comp_config_get()->first_draw_delay, 
                     _e_comp_wl_cb_first_draw, ec);
}

static void 
_e_comp_wl_cb_hook_client_eval_fetch(void *data EINA_UNUSED, E_Client *ec)
{
   E_COMP_WL_PIXMAP_CHECK;

   /* DBG("COMP_WL HOOK CLIENT EVAL FETCH !!"); */

   if ((ec->changes.prop) || (ec->netwm.fetch.state))
     {
        e_hints_window_state_get(ec);
        ec->netwm.fetch.state = EINA_FALSE;
     }

   if ((ec->changes.prop) || (ec->e.fetch.state))
     {
        e_hints_window_e_state_get(ec);
        ec->e.fetch.state = EINA_FALSE;
     }

   if ((ec->changes.prop) || (ec->netwm.fetch.type))
     {
        e_hints_window_type_get(ec);
        if (((!ec->lock_border) || (!ec->border.name)) && 
            (ec->comp_data->reparented))
          {
             ec->border.changed = EINA_TRUE;
             EC_CHANGED(ec);
          }

        if ((ec->netwm.type == E_WINDOW_TYPE_DOCK) || (ec->tooltip))
          {
             if (!ec->netwm.state.skip_pager)
               {
                  ec->netwm.state.skip_pager = EINA_TRUE;
                  ec->netwm.update.state = EINA_TRUE;
               }
             if (!ec->netwm.state.skip_taskbar)
               {
                  ec->netwm.state.skip_taskbar = EINA_TRUE;
                  ec->netwm.update.state = EINA_TRUE;
               }
          }
        else if (ec->netwm.type == E_WINDOW_TYPE_DESKTOP)
          {
             ec->focus_policy_override = E_FOCUS_CLICK;
             if (!ec->netwm.state.skip_pager)
               {
                  ec->netwm.state.skip_pager = EINA_TRUE;
                  ec->netwm.update.state = EINA_TRUE;
               }
             if (!ec->netwm.state.skip_taskbar)
               {
                  ec->netwm.state.skip_taskbar = EINA_TRUE;
                  ec->netwm.update.state = EINA_TRUE;
               }
             if (!e_client_util_ignored_get(ec))
               ec->border.changed = ec->borderless = EINA_TRUE;
          }

        if (ec->tooltip)
          {
             ec->icccm.accepts_focus = EINA_FALSE;
             eina_stringshare_replace(&ec->bordername, "borderless");
          }
        else if (ec->internal)
          {
             /* TODO: transient set */
          }

        /* TODO: raise property event */

        ec->netwm.fetch.type = EINA_FALSE;
     }

   /* TODO: vkbd, etc */

   if (ec->changes.shape)
     {
        Eina_Rectangle *shape = NULL;
        Eina_Bool pshaped = EINA_FALSE;

        shape = eina_rectangle_new((ec->comp_data->shape)->x, 
                                   (ec->comp_data->shape)->y, 
                                   (ec->comp_data->shape)->w, 
                                   (ec->comp_data->shape)->h);

        pshaped = ec->shaped;
        ec->changes.shape = EINA_FALSE;

        if (eina_rectangle_is_empty(shape))
          {
             if ((ec->shaped) && (ec->comp_data->reparented) && 
                 (!ec->bordername))
               {
                  ec->border.changed = EINA_TRUE;
                  EC_CHANGED(ec);
               }

             ec->shaped = EINA_FALSE;
          }
        else
          {
             int cw = 0, ch = 0;

             if (ec->border_size)
               {
                  shape->x += ec->border_size;
                  shape->y += ec->border_size;
                  shape->w -= ec->border_size;
                  shape->h -= ec->border_size;
               }

             e_pixmap_size_get(ec->pixmap, &cw, &ch);
             if ((cw != ec->client.w) || (ch != ec->client.h))
               {
                  ec->changes.shape = EINA_TRUE;
                  EC_CHANGED(ec);
               }

             if ((shape->x == 0) && (shape->y == 0) && 
                 (shape->w == cw) && (shape->h == ch))
               {
                  if (ec->shaped)
                    {
                       ec->shaped = EINA_FALSE;
                       if ((ec->comp_data->reparented) && (!ec->bordername))
                         {
                            ec->border.changed = EINA_TRUE;
                            EC_CHANGED(ec);
                         }
                    }
               }
             else
               {
                  if (ec->comp_data->reparented)
                    {
                       EINA_RECTANGLE_SET(ec->comp_data->shape, 
                                          shape->x, shape->y, 
                                          shape->w, shape->h);

                       if ((!ec->shaped) && (!ec->bordername))
                         {
                            ec->border.changed = EINA_TRUE;
                            EC_CHANGED(ec);
                         }
                    }
                  else
                    {
                       if (_e_comp_wl_client_shape_check(ec))
                         e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
                    }
                  ec->shaped = EINA_TRUE;
                  ec->changes.shape_input = EINA_FALSE;
                  ec->shape_input_rects_num = 0;
               }

             if (ec->shape_changed)
               e_comp_object_frame_theme_set(ec->frame, 
                                             E_COMP_OBJECT_FRAME_RESHADOW);
          }

        if (ec->shaped != pshaped)
          {
             _e_comp_wl_client_shape_check(ec);
          }

        ec->need_shape_merge = EINA_TRUE;

        eina_rectangle_free(shape);
     }

   if ((ec->changes.prop) || (ec->netwm.update.state))
     {
        e_hints_window_state_set(ec);
        if (((!ec->lock_border) || (!ec->border.name)) && 
            (!(((ec->maximized & E_MAXIMIZE_TYPE) == E_MAXIMIZE_FULLSCREEN))) && 
               (ec->comp_data->reparented))
          {
             ec->border.changed = EINA_TRUE;
             EC_CHANGED(ec);
          }

        if (ec->parent)
          {
             if (ec->netwm.state.modal)
               {
                  ec->parent->modal = ec;
                  if (ec->parent->focused)
                    evas_object_focus_set(ec->frame, EINA_TRUE);
               }
          }
        else if (ec->leader)
          {
             if (ec->netwm.state.modal)
               {
                  ec->leader->modal = ec;
                  if (ec->leader->focused)
                    evas_object_focus_set(ec->frame, EINA_TRUE);
                  else
                    {
                       Eina_List *l;
                       E_Client *child;

                       EINA_LIST_FOREACH(ec->leader->group, l, child)
                         {
                            if ((child != ec) && (child->focused))
                              evas_object_focus_set(ec->frame, EINA_TRUE);
                         }
                    }
               }
          }
        ec->netwm.update.state = EINA_FALSE;
     }

   ec->changes.prop = EINA_FALSE;
   if (!ec->comp_data->reparented) ec->changes.border = EINA_FALSE;
   if (ec->changes.icon)
     {
        if (ec->comp_data->reparented) return;
        ec->comp_data->change_icon = EINA_TRUE;
        ec->changes.icon = EINA_FALSE;
     }
}

static void 
_e_comp_wl_cb_hook_client_pre_frame(void *data EINA_UNUSED, E_Client *ec)
{
   E_COMP_WL_PIXMAP_CHECK;
   if (!ec->comp_data->need_reparent) return;

   ec->border_size = 0;
   ec->border.changed = EINA_TRUE;

   /* if (ec->shaped_input) */
   /*   { */
   /*      DBG("\tClient Shaped Input"); */
   /*   } */

   /* ec->changes.shape = EINA_TRUE; */
   /* ec->changes.shape_input = EINA_TRUE; */

   EC_CHANGED(ec);

   if (ec->visible)
     {
        /* FIXME: Other window types */
        if ((ec->comp_data->set_win_type) && (ec->internal_ecore_evas))
          {
             E_Win *ewin;

             if ((ewin = ecore_evas_data_get(ec->internal_ecore_evas, "E_Win")))
               {
                  Ecore_Wl_Window *wwin;

                  wwin = ecore_evas_wayland_window_get(ec->internal_ecore_evas);
                  ecore_wl_window_type_set(wwin, ECORE_WL_WINDOW_TYPE_TOPLEVEL);
               }

             ec->comp_data->set_win_type = EINA_FALSE;
          }
     }

   _e_comp_wl_client_evas_init(ec);

   if ((ec->netwm.ping) && (!ec->ping_poller)) 
     e_client_ping(ec);

   ec->comp_data->need_reparent = EINA_FALSE;
   ec->redirected = EINA_TRUE;

   if (ec->comp_data->change_icon)
     {
        ec->changes.icon = EINA_TRUE;
        EC_CHANGED(ec);
     }

   ec->comp_data->change_icon = EINA_FALSE;
   ec->comp_data->reparented = EINA_TRUE;
}

static void 
_e_comp_wl_cb_hook_client_post_new(void *data EINA_UNUSED, E_Client *ec)
{
   E_COMP_WL_PIXMAP_CHECK;

   if (ec->need_shape_merge) ec->need_shape_merge = EINA_FALSE;

   if (ec->need_shape_export) 
     {
        _e_comp_wl_client_shape_check(ec);
        ec->need_shape_export = EINA_FALSE;
     }
}

static void 
_e_comp_wl_cb_hook_client_eval_end(void *data EINA_UNUSED, E_Client *ec)
{
   E_COMP_WL_PIXMAP_CHECK;

   if ((ec->comp->comp_data->restack) && (!ec->comp->new_clients))
     {
        e_hints_client_stacking_set();
        ec->comp->comp_data->restack = EINA_FALSE;
     }
}

static void 
_e_comp_wl_cb_hook_client_focus_set(void *data EINA_UNUSED, E_Client *ec)
{
   if ((!ec) || (!ec->comp_data)) return;

   /* FIXME: We cannot use e_grabinput_focus calls here */

   if (ec->comp_data->shell.activate)
     {
        if (ec->comp_data->shell.surface)
          ec->comp_data->shell.activate(ec->comp_data->shell.surface);
     }
}

static void 
_e_comp_wl_cb_hook_client_focus_unset(void *data EINA_UNUSED, E_Client *ec)
{
   if ((!ec) || (!ec->comp_data)) return;

   if (ec->comp_data->shell.deactivate)
     {
        if (ec->comp_data->shell.surface)
          ec->comp_data->shell.deactivate(ec->comp_data->shell.surface);
     }
}

EAPI Eina_Bool 
e_comp_wl_init(void)
{
   /* set gl available */
   if (ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_WAYLAND_EGL))
     e_comp_gl_set(EINA_TRUE);

   /* try to create a wayland compositor */
   if (!_e_comp_wl_compositor_create())
     {
        e_error_message_show(_("Enlightenment cannot create a Wayland Compositor!\n"));
        return EINA_FALSE;
     }

   /* set ecore_wayland in server mode
    * NB: this is done before init so that ecore_wayland does not stall while 
    * waiting for compositor to be created */
   /* ecore_wl_server_mode_set(EINA_TRUE); */

   /* try to initialize ecore_wayland */
   if (!ecore_wl_init(NULL))
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore_Wayland!\n"));
        return EINA_FALSE;
     }

   /* create hash to store client windows */
   clients_win_hash = eina_hash_int32_new(NULL);

   /* setup event handlers for e events */
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_COMP_OBJECT_ADD, 
                         _e_comp_wl_cb_comp_object_add, NULL);
   /* E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_ZONE_SET,  */
   /*                       _e_comp_wl_cb_client_zone_set, NULL); */
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_PROPERTY, 
                         _e_comp_wl_cb_client_prop, NULL);

   /* setup client hooks */
   /* e_client_hook_add(E_CLIENT_HOOK_DESK_SET, _cb, NULL); */
   /* e_client_hook_add(E_CLIENT_HOOK_RESIZE_BEGIN, _cb, NULL); */
   /* e_client_hook_add(E_CLIENT_HOOK_RESIZE_END, _cb, NULL); */
   /* e_client_hook_add(E_CLIENT_HOOK_MOVE_BEGIN,  */
   /*                   _e_comp_wl_cb_hook_client_move_begin, NULL); */
   e_client_hook_add(E_CLIENT_HOOK_MOVE_END, 
                     _e_comp_wl_cb_hook_client_move_end, NULL);
   e_client_hook_add(E_CLIENT_HOOK_DEL, 
                     _e_comp_wl_cb_hook_client_del, NULL);
   e_client_hook_add(E_CLIENT_HOOK_NEW_CLIENT, 
                     _e_comp_wl_cb_hook_client_new, NULL);
   e_client_hook_add(E_CLIENT_HOOK_EVAL_FETCH, 
                     _e_comp_wl_cb_hook_client_eval_fetch, NULL);
   e_client_hook_add(E_CLIENT_HOOK_EVAL_PRE_FRAME_ASSIGN, 
                     _e_comp_wl_cb_hook_client_pre_frame, NULL);
   e_client_hook_add(E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT, 
                     _e_comp_wl_cb_hook_client_post_new, NULL);
   e_client_hook_add(E_CLIENT_HOOK_EVAL_END, 
                     _e_comp_wl_cb_hook_client_eval_end, NULL);
   /* e_client_hook_add(E_CLIENT_HOOK_UNREDIRECT, _cb, NULL); */
   /* e_client_hook_add(E_CLIENT_HOOK_REDIRECT, _cb, NULL); */
   e_client_hook_add(E_CLIENT_HOOK_FOCUS_SET, 
                     _e_comp_wl_cb_hook_client_focus_set, NULL);
   e_client_hook_add(E_CLIENT_HOOK_FOCUS_UNSET, 
                     _e_comp_wl_cb_hook_client_focus_unset, NULL);

   /* TODO: e_desklock_hooks ?? */

   return EINA_TRUE;
}

EINTERN void 
e_comp_wl_shutdown(void)
{
   /* delete event handlers */
   E_FREE_LIST(handlers, ecore_event_handler_del);

   /* delete client window hash */
   E_FREE_FUNC(clients_win_hash, eina_hash_free);

   /* shutdown ecore_wayland */
   ecore_wl_shutdown();
}

EINTERN struct wl_resource *
e_comp_wl_surface_create(struct wl_client *client, int version, uint32_t id)
{
   struct wl_resource *ret = NULL;

   if ((ret = wl_resource_create(client, &wl_surface_interface, version, id)))
     wl_resource_set_implementation(ret, &_e_comp_wl_surface_interface, NULL, 
                                    e_comp_wl_surface_destroy);

   return ret;
}

EINTERN void 
e_comp_wl_surface_destroy(struct wl_resource *resource)
{
   E_Pixmap *cp;
   E_Client *ec;

   if (!(cp = wl_resource_get_user_data(resource))) return;

   /* try to find the E client for this surface */
   if (!(ec = e_pixmap_client_get(cp)))
     ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, e_pixmap_window_get(cp));

   if (!ec)
     {
        e_pixmap_free(cp);
        return;
     }

   e_object_del(E_OBJECT(ec));
}
