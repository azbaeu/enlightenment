#include "e.h"
#include "e_mod_main.h"
#include "e_mod_comp.h"
#ifdef HAVE_WAYLAND
# include "e_mod_comp_wl.h"
# include "e_mod_comp_wl_buffer.h"
# include "e_mod_comp_wl_comp.h"
#endif

void 
e_mod_comp_wl_buffer_post_release(struct wl_buffer *buffer)
{
   if (--buffer->busy_count > 0) return;
   if (buffer->resource.client)
     wl_resource_queue_event(&buffer->resource, WL_BUFFER_RELEASE);
}

void 
e_mod_comp_wl_buffer_attach(struct wl_buffer *buffer, struct wl_surface *surface)
{
   Wayland_Surface *ws;

   LOGFN(__FILE__, __LINE__, __FUNCTION__);

   ws = (Wayland_Surface *)surface;

   if (ws->saved_texture != 0)
     ws->texture = ws->saved_texture;

   glBindTexture(GL_TEXTURE_2D, ws->texture);

   if (wl_buffer_is_shm(buffer))
     {
        struct wl_list *attached;

        ws->pitch = wl_shm_buffer_get_stride(buffer) / 4;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, ws->pitch, buffer->height, 
                     0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 
                     wl_shm_buffer_get_data(buffer));
        switch (wl_shm_buffer_get_format(buffer))
          {
           case WL_SHM_FORMAT_ARGB8888:
             ws->visual = WAYLAND_ARGB_VISUAL;
             break;
           case WL_SHM_FORMAT_XRGB8888:
             ws->visual = WAYLAND_RGB_VISUAL;
             break;
          }
        attached = buffer->user_data;
        wl_list_remove(&ws->buffer_link);
        wl_list_insert(attached, &ws->buffer_link);
     }
   else 
     {
        Wayland_Compositor *comp;

        comp = e_mod_comp_wl_comp_get();
        if (ws->image != EGL_NO_IMAGE_KHR)
          comp->destroy_image(comp->egl.display, ws->image);
        ws->image = comp->create_image(comp->egl.display, NULL, 
                                       EGL_WAYLAND_BUFFER_WL, buffer, NULL);
        comp->image_target_texture_2d(GL_TEXTURE_2D, ws->image);
        ws->visual = WAYLAND_ARGB_VISUAL;
        ws->pitch = ws->w;
     }
}
