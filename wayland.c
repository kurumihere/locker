#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-session-lock-v1.h"
#include "pam_auth.h"
#include "util.h"
#include "wayland.h"

struct wl_output_info {
        struct wl_list link;
        uint32_t name;
        struct wl_output *output;
        struct wl_surface *surface;
        struct ext_session_lock_surface_v1 *lock_surface;
        struct wl_buffer *buffer;
        void *shm_data;
        int w, h;
        int configured;
};

struct wl_state {
        struct wl_display *display;
        struct wl_registry *registry;
        struct wl_compositor *compositor;
        struct wl_shm *shm;
        struct wl_seat *seat;
        struct wl_keyboard *keyboard;
        struct ext_session_lock_manager_v1 *lock_manager;
        struct ext_session_lock_v1 *lock;
        struct wl_list outputs;
        int running;
        int locked;
        char password[256];
        int pos;
        char *username;
        struct xkb_context *xkb_ctx;
        struct xkb_keymap *xkb_keymap;
        struct xkb_state *xkb_state;
};

/* registry */

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
                const char *interface, uint32_t version)
{
        struct wl_state *wl = data;
        (void)version;

        if (strcmp(interface, "wl_compositor") == 0) {
                wl->compositor = wl_registry_bind(registry, name,
                                                  &wl_compositor_interface, 4);
        } else if (strcmp(interface, "wl_shm") == 0) {
                wl->shm =
                    wl_registry_bind(registry, name, &wl_shm_interface, 1);
        } else if (strcmp(interface, "wl_seat") == 0) {
                wl->seat =
                    wl_registry_bind(registry, name, &wl_seat_interface, 7);
        } else if (strcmp(interface, "wl_output") == 0) {
                struct wl_output_info *oi = calloc(1, sizeof(*oi));
                if (!oi)
                        return;
                oi->name = name;
                oi->w = 16;
                oi->h = 16;
                oi->output =
                    wl_registry_bind(registry, name, &wl_output_interface, 2);
                wl_list_insert(wl->outputs.prev, &oi->link);
        } else if (strcmp(interface, "ext_session_lock_manager_v1") == 0) {
                wl->lock_manager = wl_registry_bind(
                    registry, name, &ext_session_lock_manager_v1_interface, 1);
        }
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
        struct wl_state *wl = data;
        struct wl_output_info *oi, *tmp;
        (void)registry;

        wl_list_for_each_safe(oi, tmp, &wl->outputs, link)
        {
                if (oi->name == name) {
                        wl_list_remove(&oi->link);
                        if (oi->buffer)
                                wl_buffer_destroy(oi->buffer);
                        if (oi->shm_data)
                                munmap(oi->shm_data, oi->w * oi->h * 4);
                        if (oi->lock_surface)
                                ext_session_lock_surface_v1_destroy(
                                    oi->lock_surface);
                        if (oi->surface)
                                wl_surface_destroy(oi->surface);
                        wl_output_destroy(oi->output);
                        free(oi);
                        break;
                }
        }
}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_global_remove};

/* output */

static void
output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
                int32_t w, int32_t h, int32_t subpixel, const char *make,
                const char *model, int32_t transform)
{
        (void)data;
        (void)output;
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)subpixel;
        (void)make;
        (void)model;
        (void)transform;
}

static void
output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t w,
            int32_t h, int32_t refresh)
{
        struct wl_output_info *oi = data;
        (void)output;
        (void)refresh;

        if (!(flags & WL_OUTPUT_MODE_CURRENT))
                return;

        oi->w = w;
        oi->h = h;
}

static void
output_done(void *data, struct wl_output *output)
{
        (void)data;
        (void)output;
}

static void
output_scale(void *data, struct wl_output *output, int32_t factor)
{
        (void)data;
        (void)output;
        (void)factor;
}

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale,
    NULL, NULL};

/* buffer */

static int
create_shm_fd(size_t size)
{
        char name[] = "/tmp/locker-XXXXXX";
        int fd = mkstemp(name);
        if (fd < 0)
                return -1;
        unlink(name);
        if (ftruncate(fd, (off_t)size) < 0) {
                close(fd);
                return -1;
        }
        return fd;
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
        (void)data;
        (void)buffer;
}

static const struct wl_buffer_listener buffer_listener = {buffer_release};

static int
make_buffer(struct wl_state *wl, struct wl_output_info *oi, unsigned long pixel)
{
        size_t stride = oi->w * 4;
        size_t size = stride * oi->h;

        int fd = create_shm_fd(size);
        if (fd < 0)
                return -1;

        void *data =
            mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
                close(fd);
                return -1;
        }

        uint32_t *px = data;
        for (int i = 0; i < oi->w * oi->h; i++)
                px[i] = (uint32_t)pixel;

        struct wl_shm_pool *pool = wl_shm_create_pool(wl->shm, fd, size);
        oi->buffer = wl_shm_pool_create_buffer(pool, 0, oi->w, oi->h, stride,
                                               WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(oi->buffer, &buffer_listener, NULL);
        wl_shm_pool_destroy(pool);

        oi->shm_data = data;
        close(fd);
        return 0;
}

/* lock surface */

static void
lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *ls,
                       uint32_t serial, uint32_t w, uint32_t h)
{
        struct wl_output_info *oi = data;
        (void)ls;

        ext_session_lock_surface_v1_ack_configure(ls, serial);

        if (oi->buffer)
                wl_buffer_destroy(oi->buffer);
        if (oi->shm_data) {
                munmap(oi->shm_data, oi->h * oi->w * 4);
                oi->shm_data = NULL;
        }

        oi->w = w;
        oi->h = h;
        oi->configured = 1;
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener =
    {lock_surface_configure};

/* lock */

static void
lock_finished(void *data, struct ext_session_lock_v1 *lock)
{
        struct wl_state *wl = data;
        wl->running = 0;
        (void)lock;
}

static void
lock_locked(void *data, struct ext_session_lock_v1 *lock)
{
        struct wl_state *wl = data;
        wl->locked = 1;
        (void)lock;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    lock_locked, lock_finished};

/* seat */

static void
seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
        struct wl_state *wl = data;

        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->keyboard) {
                wl->keyboard = wl_seat_get_keyboard(seat);
        } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl->keyboard) {
                wl_keyboard_release(wl->keyboard);
                wl->keyboard = NULL;
        }
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
        (void)data;
        (void)seat;
        (void)name;
}

static const struct wl_seat_listener seat_listener = {seat_capabilities,
                                                       seat_name};

/* keyboard */

static void
keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
                int32_t fd, uint32_t size)
{
        struct wl_state *wl = data;
        (void)keyboard;

        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
                close(fd);
                return;
        }

        char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map_str == MAP_FAILED) {
                close(fd);
                return;
        }

        struct xkb_keymap *new_keymap = xkb_keymap_new_from_string(
            wl->xkb_ctx, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
        munmap(map_str, size);
        close(fd);

        if (!new_keymap)
                return;

        struct xkb_state *new_state = xkb_state_new(new_keymap);
        if (!new_state) {
                xkb_keymap_unref(new_keymap);
                return;
        }

        if (wl->xkb_state)
                xkb_state_unref(wl->xkb_state);
        if (wl->xkb_keymap)
                xkb_keymap_unref(wl->xkb_keymap);
        wl->xkb_keymap = new_keymap;
        wl->xkb_state = new_state;
}

static void
keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
               struct wl_surface *surface, struct wl_array *keys)
{
        (void)data;
        (void)keyboard;
        (void)serial;
        (void)surface;
        (void)keys;
}

static void
keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
               struct wl_surface *surface)
{
        (void)data;
        (void)keyboard;
        (void)serial;
        (void)surface;
}

static void
keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
             uint32_t time, uint32_t key, uint32_t state)
{
        struct wl_state *wl = data;
        (void)keyboard;
        (void)serial;
        (void)time;

        if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
                return;

        if (!wl->xkb_state)
                return;

        xkb_keysym_t sym = xkb_state_key_get_one_sym(wl->xkb_state, key + 8);
        if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
                int rc = locker_pam_auth(wl->username, wl->password);
                if (rc == 0) {
                        secure_zero(wl->password, sizeof(wl->password));
                        wl->running = 0;
                } else {
                        secure_zero(wl->password, sizeof(wl->password));
                        wl->pos = 0;
                }
        } else if (sym == XKB_KEY_BackSpace) {
                if (wl->pos > 0) {
                        wl->pos--;
                        wl->password[wl->pos] = '\0';
                }
        } else if (sym == XKB_KEY_Escape) {
                secure_zero(wl->password, sizeof(wl->password));
                wl->pos = 0;
        } else {
                char buf[8];
                int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
                if (n > 0 && wl->pos + n < (int)sizeof(wl->password) - 1) {
                        memcpy(wl->password + wl->pos, buf, n);
                        wl->pos += n;
                        wl->password[wl->pos] = '\0';
                }
        }
        /* clear password feedback would go here */
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                   uint32_t mods_depressed, uint32_t mods_latched,
                   uint32_t mods_locked, uint32_t group)
{
        struct wl_state *wl = data;
        (void)keyboard;
        (void)serial;

        if (wl->xkb_state)
                xkb_state_update_mask(wl->xkb_state, mods_depressed,
                                      mods_latched, mods_locked, 0, 0, group);
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
                     int32_t delay)
{
        (void)data;
        (void)keyboard;
        (void)rate;
        (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap, keyboard_enter,     keyboard_leave,
    keyboard_key,    keyboard_modifiers, keyboard_repeat_info};

/* main */

int
wayland_run(int blur_radius, double darken, const char *bg_color)
{
        (void)blur_radius;
        (void)darken;

        struct wl_state wl = {0};
        wl_list_init(&wl.outputs);
        wl.running = 1;

        /* compute background pixel */
        unsigned long bg_pixel = 0xFF1a1a2e;
        if (bg_color) {
                unsigned long rgb;
                if (parse_hex(bg_color, &rgb) != 0) {
                        fprintf(stderr, "wayland: invalid color '%s'\n",
                                bg_color);
                        return 1;
                }
                unsigned char r = (rgb >> 16) & 0xFF;
                unsigned char g = (rgb >> 8) & 0xFF;
                unsigned char b = rgb & 0xFF;
                bg_pixel = (0xFFUL << 24) | (r << 16) | (g << 8) | b;
        }

        /* xkb context */
        wl.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!wl.xkb_ctx) {
                fprintf(stderr, "wayland: failed to create xkb context\n");
                return 1;
        }

        /* username */
        wl.username = getenv("USER");
        if (!wl.username) {
                fprintf(stderr, "wayland: $USER not set\n");
                xkb_context_unref(wl.xkb_ctx);
                return 1;
        }

        /* connect */
        wl.display = wl_display_connect(NULL);
        if (!wl.display) {
                fprintf(stderr, "wayland: cannot connect to display\n");
                xkb_context_unref(wl.xkb_ctx);
                return 1;
        }

        /* registry */
        wl.registry = wl_display_get_registry(wl.display);
        wl_registry_add_listener(wl.registry, &registry_listener, &wl);
        wl_display_roundtrip(wl.display);

        /* check required globals */
        if (!wl.compositor || !wl.shm || !wl.lock_manager) {
                fprintf(stderr, "wayland: missing required globals\n");
                wl_display_disconnect(wl.display);
                xkb_context_unref(wl.xkb_ctx);
                return 1;
        }

        /* seat listener + roundtrip for keyboard */
        if (wl.seat)
                wl_seat_add_listener(wl.seat, &seat_listener, &wl);
        wl_display_roundtrip(wl.display);

        /* bind output listeners and create lock surfaces */
        {
                struct wl_output_info *oi;
                wl_list_for_each(oi, &wl.outputs, link)
                {
                        wl_output_add_listener(oi->output, &output_listener,
                                               oi);
                }
        }
        wl_display_roundtrip(wl.display);

        /* lock */
        wl.lock = ext_session_lock_manager_v1_lock(wl.lock_manager);
        ext_session_lock_v1_add_listener(wl.lock, &lock_listener, &wl);

        /* create lock surfaces for all outputs */
        {
                struct wl_output_info *oi;
                wl_list_for_each(oi, &wl.outputs, link)
                {
                        oi->surface =
                            wl_compositor_create_surface(wl.compositor);
                        oi->lock_surface = ext_session_lock_v1_get_lock_surface(
                            wl.lock, oi->surface, oi->output);
                        ext_session_lock_surface_v1_add_listener(
                            oi->lock_surface, &lock_surface_listener, oi);
                }
        }
        wl_display_roundtrip(wl.display);

        if (!wl.running) {
                fprintf(stderr, "wayland: lock request rejected\n");
                wl_display_disconnect(wl.display);
                xkb_context_unref(wl.xkb_ctx);
                return 1;
        }

        /* now that we're locked, attach buffers to surfaces */
        {
                struct wl_output_info *oi;
                wl_list_for_each(oi, &wl.outputs, link)
                {
                        if (oi->w > 0 && oi->h > 0) {
                                make_buffer(&wl, oi, bg_pixel);
                                wl_surface_attach(oi->surface, oi->buffer, 0,
                                                  0);
                                wl_surface_commit(oi->surface);
                        }
                }
        }
        wl_display_roundtrip(wl.display);

        /* keyboard listener */
        if (wl.keyboard)
                wl_keyboard_add_listener(wl.keyboard, &keyboard_listener, &wl);

        /* main loop */
        while (wl.running && wl_display_dispatch(wl.display) >= 0)
                ;

        /* cleanup lock surfaces */
        {
                struct wl_output_info *oi, *tmp;
                wl_list_for_each_safe(oi, tmp, &wl.outputs, link)
                {
                        wl_list_remove(&oi->link);
                        if (oi->buffer)
                                wl_buffer_destroy(oi->buffer);
                        if (oi->shm_data)
                                munmap(oi->shm_data, oi->w * oi->h * 4);
                        if (oi->lock_surface)
                                ext_session_lock_surface_v1_destroy(
                                    oi->lock_surface);
                        if (oi->surface)
                                wl_surface_destroy(oi->surface);
                        wl_output_destroy(oi->output);
                        free(oi);
                }
        }

        /* cleanup */
        if (wl.keyboard)
                wl_keyboard_release(wl.keyboard);
        if (wl.xkb_state)
                xkb_state_unref(wl.xkb_state);
        if (wl.xkb_keymap)
                xkb_keymap_unref(wl.xkb_keymap);
        if (wl.lock)
                ext_session_lock_v1_unlock_and_destroy(wl.lock);
        wl_display_disconnect(wl.display);
        xkb_context_unref(wl.xkb_ctx);

        return 0;
}
