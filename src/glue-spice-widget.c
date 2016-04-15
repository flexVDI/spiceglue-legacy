/**
 * Copyright (C) 2016 flexVDI (Flexible Software Solutions S.L.)
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_X11_XKBLIB_H
#include <X11/XKBlib.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#endif
#ifdef WIN32
#include <windows.h>
#ifndef MAPVK_VK_TO_VSC /* may be undefined in older mingw-headers */
#define MAPVK_VK_TO_VSC 0
#endif
#endif

#include "glue-spice-widget.h"
#include "glue-spice-widget-priv.h"
#include "glue-service.h"
#include "mono-glue-types.h"


G_DEFINE_TYPE(SpiceDisplay, spice_display, SPICE_TYPE_CHANNEL);

/* Signals */
enum {
    SPICE_DISPLAY_MOUSE_GRAB,
    SPICE_DISPLAY_KEYBOARD_GRAB,
    SPICE_DISPLAY_GRAB_KEY_PRESSED,
    SPICE_DISPLAY_LAST_SIGNAL,
};

static guint signals[SPICE_DISPLAY_LAST_SIGNAL];

#ifdef WIN32
static HWND win32_window = NULL;
#endif


static void disconnect_main(SpiceDisplay *display);
static void disconnect_display(SpiceDisplay *display);
static void disconnect_cursor(SpiceDisplay *display);
static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void sync_keyboard_lock_modifiers(SpiceDisplay *display);
static void try_mouse_ungrab(SpiceDisplay *display);

int16_t SpiceGlibGlueOnGainFocus();

static gint get_display_id(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    /* supported monitor_id only with display channel #0 */
    if (d->channel_id == 0 && d->monitor_id >= 0)
	return d->monitor_id;

    g_return_val_if_fail(d->monitor_id <= 0, -1);

    return d->channel_id;
}
/* ---------------------------------------------------------------- */

static void spice_display_dispose(GObject *obj)
{
    SpiceDisplay *display = SPICE_DISPLAY(obj);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    SPICE_DEBUG("spice display dispose");

    disconnect_main(display);
    disconnect_display(display);
    disconnect_cursor(display);

    //if (d->clipboard) {
    //    g_signal_handlers_disconnect_by_func(d->clipboard, G_CALLBACK(clipboard_owner_change),
    //                                         display);
    //    d->clipboard = NULL;
    //}

    //if (d->clipboard_primary) {
    //    g_signal_handlers_disconnect_by_func(d->clipboard_primary, G_CALLBACK(clipboard_owner_change),
    //                                         display);
    //    d->clipboard_primary = NULL;
    //}
    if (d->session) {
	g_signal_handlers_disconnect_by_func(d->session, G_CALLBACK(channel_new),
					     display);
	g_signal_handlers_disconnect_by_func(d->session, G_CALLBACK(channel_destroy),
					     display);
	g_object_unref(d->session);
	d->session = NULL;
    }
}

static void spice_display_finalize(GObject *obj)
{
    SPICE_DEBUG("Finalize spice display");
    G_OBJECT_CLASS(spice_display_parent_class)->finalize(obj);
}


static void spice_display_class_init(SpiceDisplayClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    // FIXME Object broken in glue. closures are not connected here
    /**
     * SpiceDisplay::mouse-grab:
     * @display: the #SpiceDisplay that emitted the signal
     * @status: 1 if grabbed, 0 otherwise.
     *
     * Notify when the mouse grab is active or not.
     **/
    signals[SPICE_DISPLAY_MOUSE_GRAB] =
	g_signal_new("mouse-grab",
		     G_OBJECT_CLASS_TYPE(gobject_class),
		     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(SpiceDisplayClass, mouse_grab),
		     NULL, NULL,
		     g_cclosure_marshal_VOID__INT,
		     G_TYPE_NONE,
		     1,
		     G_TYPE_INT);


    // TODO ver:
    /**
     * SpiceDisplay::keyboard-grab:
     * @display: the #SpiceDisplay that emitted the signal
     * @status: 1 if grabbed, 0 otherwise.
     *
     * Notify when the keyboard grab is active or not.
     **/
    /*signals[SPICE_DISPLAY_KEYBOARD_GRAB] =
      g_signal_new("keyboard-grab",
      G_OBJECT_CLASS_TYPE(gobject_class),
      G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET(SpiceDisplayClass, keyboard_grab),
      NULL, NULL,
      g_cclosure_marshal_VOID__INT,
      G_TYPE_NONE,
      1,
      G_TYPE_INT);*/

    g_type_class_add_private(klass, sizeof(SpiceDisplayPrivate));
}


static void spice_display_init(SpiceDisplay *display)
{
    SPICE_DEBUG("%s", __FUNCTION__);
    global_display = display;
    SpiceDisplayPrivate *d;

    d = display->priv = SPICE_DISPLAY_GET_PRIVATE(display);
    memset(d, 0, sizeof(*d));
    d->have_mitshm = true;
    d->mouse_last_x = -1;
    d->mouse_last_y = -1;

    d->resize_guest_enable=true;
    SpiceGlibGlueOnGainFocus();

    STATIC_MUTEX_INIT(d->cursor_lock);
}

/* ---------------------------------------------------------------- */

static void set_mouse_accel(SpiceDisplay *display, gboolean enabled)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

#if defined GDK_WINDOWING_X11
    GdkWindow *w = GDK_WINDOW(gtk_widget_get_window(GTK_WIDGET(display)));

    if (!GDK_IS_X11_DISPLAY(gdk_window_get_display(w))) {
	SPICE_DEBUG("FIXME: gtk backend is not X11");
	return;
    }

    Display *x_display = GDK_WINDOW_XDISPLAY(w);
    if (enabled) {
	/* restore mouse acceleration */
	XChangePointerControl(x_display, True, True,
			      d->x11_accel_numerator, d->x11_accel_denominator, d->x11_threshold);
    } else {
	XGetPointerControl(x_display,
			   &d->x11_accel_numerator, &d->x11_accel_denominator, &d->x11_threshold);
	/* set mouse acceleration to default */
	XChangePointerControl(x_display, True, True, -1, -1, -1);
	SPICE_DEBUG("disabled X11 mouse motion %d %d %d",
		    d->x11_accel_numerator, d->x11_accel_denominator, d->x11_threshold);
    }
#elif defined GDK_WINDOWING_WIN32
    if (enabled) {
	g_return_if_fail(SystemParametersInfo(SPI_SETMOUSE, 0, &d->win_mouse, 0));
	g_return_if_fail(SystemParametersInfo(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)d->win_mouse_speed, 0));
    } else {
	int accel[3] = { 0, 0, 0 }; // disabled
	g_return_if_fail(SystemParametersInfo(SPI_GETMOUSE, 0, &d->win_mouse, 0));
	g_return_if_fail(SystemParametersInfo(SPI_GETMOUSESPEED, 0, &d->win_mouse_speed, 0));
	g_return_if_fail(SystemParametersInfo(SPI_SETMOUSE, 0, &accel, SPIF_SENDCHANGE));
	g_return_if_fail(SystemParametersInfo(SPI_SETMOUSESPEED, 0, (PVOID)10, SPIF_SENDCHANGE)); // default
    }
#else
    g_warning("Mouse acceleration code missing for your platform");
#endif
}

#ifdef WIN32
void SpiceGlibSetWindowHwnd(HWND h) {
    win32_window= h;
    SPICE_DEBUG("SpiceGlibSetWindowHwnd! %p", win32_window);
}

static gboolean win32_clip_cursor(void)
{
    RECT window, workarea, rect;
    HMONITOR monitor;
    MONITORINFO mi = { 0, };

    SPICE_DEBUG("%s win32_clip_cursor pointer windowHwnd: %p", __FUNCTION__, win32_window);

    g_return_val_if_fail(win32_window != NULL, FALSE);

    if (!GetWindowRect(win32_window, &window)) {
	SPICE_DEBUG("ERROR calling GetWindowRect() hwnd: %p", win32_window);
	goto error;
    }

    monitor = MonitorFromRect(&window, MONITOR_DEFAULTTONEAREST);
    g_return_val_if_fail(monitor != NULL, false);
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(monitor, &mi))
	goto error;
    workarea = mi.rcWork;

    if (!IntersectRect(&rect, &window, &workarea)) {
	g_critical("error clipping cursor");
	return false;
    }

    SPICE_DEBUG("clip rect t:%ld b:%ld l:%ld r:%ld ",
		rect.top, rect.bottom, rect.left, rect.right);

    if (!ClipCursor(&rect)) {
	SPICE_DEBUG("win32 ClipCursor() failed");
	goto error;
    }

    return true;

 error:
    {
	DWORD errval  = GetLastError();
	gchar *errstr = g_win32_error_message(errval);
	g_warning("failed to clip cursor (%ld) %s", errval, errstr);
	SPICE_DEBUG("win32_clip_cursor() failed");
    }

    return false;
}
#endif

static gboolean do_pointer_grab(SpiceDisplay *display)
{
    SPICE_DEBUG("%s ini() pointer", __FUNCTION__);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    gboolean status = -1;
    SPICE_DEBUG("%s NO PASO CURSOR blank al grab;", __FUNCTION__);
#ifdef WIN32
    if (!win32_clip_cursor()) {
	SPICE_DEBUG("%s win32_clip_cursor FAILED;", __FUNCTION__);
	goto end;
    }
#endif

    SPICE_DEBUG("%s FIXME pointer No hago try_keyboard_grab(display);", __FUNCTION__);
    SPICE_DEBUG("%s FIXME pointer falta gdk_pointer_grab();", __FUNCTION__);
    
    status = 0;
    
    if (status != 0/*GDK_GRAB_SUCCESS*/) {
	d->mouse_grab_active = false;
	g_warning("pointer grab failed %d", status);
	SPICE_DEBUG("%s failed;", __FUNCTION__);
    } else {
	SPICE_DEBUG("%s mouse_grab_active=true", __FUNCTION__);
	d->mouse_grab_active = true;
	g_signal_emit(display, signals[SPICE_DISPLAY_MOUSE_GRAB], 0, true);
    }

    if (status == 0 /*GDK_GRAB_SUCCESS*/)
	set_mouse_accel(display, FALSE);
#ifdef WIN32
 end:
#endif

    return status;
}

static void try_mouse_grab(SpiceDisplay *display)
{
    SPICE_DEBUG("%s", __FUNCTION__);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    SPICE_DEBUG("%s A1 checking SPICE_NOGRAB env var ", __FUNCTION__);
    if (g_getenv("SPICE_NOGRAB"))
	return;

    SPICE_DEBUG("%s A2 checking disable_inputs", __FUNCTION__);
    if (d->disable_inputs)
	return;

    SPICE_DEBUG("%s A3 Disabled checks", __FUNCTION__);

    if (d->mouse_mode != SPICE_MOUSE_MODE_SERVER)
	return;

    SPICE_DEBUG("%s A4 checking server mode", __FUNCTION__);
    if (d->mouse_grab_active)
	return;

    SPICE_DEBUG("%s A5", __FUNCTION__);
    if (do_pointer_grab(display) != 0/*GDK_GRAB_SUCCESS*/)
	return;

    SPICE_DEBUG("%s A6", __FUNCTION__);
    d->mouse_last_x = -1;
    d->mouse_last_y = -1;
}

static void update_mouse_mode(SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    SPICE_DEBUG("%s ", __FUNCTION__);
    g_object_get(channel, "mouse-mode", &d->mouse_mode, NULL);
    SPICE_DEBUG("mouse mode %d", d->mouse_mode);

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
	try_mouse_ungrab(display);
	break;
    case SPICE_MOUSE_MODE_SERVER:
	try_mouse_grab(display);
	d->mouse_guest_x = -1;
	d->mouse_guest_y = -1;
	break;
    default:
	g_warn_if_reached();
    }

    // next line would update the cursor image if we used gtk (gdk_window_set_cursor)
    // But we update this data by polling (a-la xna)
    //update_mouse_pointer(display);
}

static void update_monitor_area(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    SpiceDisplayMonitorConfig *cfg, *c = NULL;
    GArray *monitors = NULL;
    int i;
    SPICE_DEBUG("%s: %d:%d", __FUNCTION__, d->channel_id, d->monitor_id);
    if (d->monitor_id < 0)
	goto whole;

    g_object_get(d->display, "monitors", &monitors, NULL);
    for (i = 0; monitors != NULL && i < monitors->len; i++) {
	cfg = &g_array_index(monitors, SpiceDisplayMonitorConfig, i);
	if (cfg->id == d->monitor_id) {
	    c = cfg;
	    break;
	}
    }
    if (c == NULL) {
	SPICE_DEBUG("update monitor: no monitor %d", d->monitor_id);
	//set_monitor_ready(display, false);
	if (spice_channel_test_capability(d->display, SPICE_DISPLAY_CAP_MONITORS_CONFIG)) {
	    SPICE_DEBUG("waiting until MonitorsConfig is received");
	    g_clear_pointer(&monitors, g_array_unref);
	    return;
	}
	goto whole;
    }

    if (c->surface_id != 0) {
	g_warning("FIXME: only support monitor config with primary surface 0, "
		  "but given config surface %d", c->surface_id);
	goto whole;
    }

    if (!d->resize_guest_enable) {
	SPICE_DEBUG(" -->>> spice_main_update_display ");
	spice_main_update_display(d->main, get_display_id(display),
				  c->x, c->y, c->width, c->height, FALSE);
    }

    //update_area(display, c->x, c->y, c->width, c->height);
    g_clear_pointer(&monitors, g_array_unref);
    return;

 whole:
    g_clear_pointer(&monitors, g_array_unref);
    /* by display whole surface */
    //update_area(display, 0, 0, d->width, d->height);
    //set_monitor_ready(display, true);
}

int32_t SpiceGlibRecalcGeometry(int32_t x, int32_t y, int32_t w, int32_t h) {

    if (global_display == NULL) {
	return -1;
    }
    SpiceDisplay* display = global_display;

    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->data == NULL) {
	return -1;
    }

    gdouble zoom = 1.0;

    // if (spicex_is_scaled(display)) This says if (backend X/Cairo) supports scaling images
    //  zoom = (gdouble)d->zoom_level / 100;

    SPICE_DEBUG("recalc1 geom monitor: %d:%d, guest +%d+%d, window %dx%d, zoom %g",
		d->channel_id, d->monitor_id,
		w, h, x, y,
		zoom);
    if (d->resize_guest_enable) {
	spice_main_set_display(d->main, get_display_id(display),
			       x, y, w / zoom, h / zoom);
    }

    return 0;
}

/* ---------------------------------------------------------------- */

#define CONVERT_0565_TO_0888(s)					\
    (((((s) << 3) & 0xf8) | (((s) >> 2) & 0x7)) |		\
     ((((s) << 5) & 0xfc00) | (((s) >> 1) & 0x300)) |		\
     ((((s) << 8) & 0xf80000) | (((s) << 3) & 0x70000)))

#define CONVERT_0565_TO_8888(s) (CONVERT_0565_TO_0888(s) | 0xff000000)

#define CONVERT_0555_TO_0888(s)				\
    (((((s) & 0x001f) << 3) | (((s) & 0x001c) >> 2)) |	\
     ((((s) & 0x03e0) << 6) | (((s) & 0x0380) << 1)) |	\
     ((((s) & 0x7c00) << 9) | ((((s) & 0x7000)) << 4)))

#define CONVERT_0555_TO_8888(s) (CONVERT_0555_TO_0888(s) | 0xff000000)

void send_key(SpiceDisplay *display, int scancode, int down)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    uint32_t i, b, m;

    if (!d->inputs)
	return;

    i = scancode / 32;
    b = scancode % 32;
    m = (1 << b);
    g_return_if_fail(i < SPICE_N_ELEMENTS(d->key_state));

    if (down) {
	// send event to guest
	spice_inputs_key_press(d->inputs, scancode);
	// update local "key-is-pressed"  map
	d->key_state[i] |= m;
    } else {
	if (!(d->key_state[i] & m)) {
	    return;
	}
	spice_inputs_key_release(d->inputs, scancode);
	d->key_state[i] &= ~m;
    }
}

/* Release any key (specifically interesting for alt, shift, ... which could be pressed
 * when app lose focus.
 * Send release-key events to the guest for every key that is pressed.
 * This avoids the "stuck key" problem when widget lost focus with a key pressed
 * and did not receive the release event.
 * This generates more key events than needed, but that is not a big deal.
 */
static void release_keys(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    uint32_t i, b;

    SPICE_DEBUG("%s", __FUNCTION__);
    for (i = 0; i < SPICE_N_ELEMENTS(d->key_state); i++) {
	if (!d->key_state[i]) {
	    continue;
	}
	for (b = 0; b < 32; b++) {
	    send_key(display, i * 32 + b, FALSE);
	}
    }
}

/* ---------------------------------------------------------------- */

volatile gboolean invalidated = FALSE;
volatile gint invalidate_x;
volatile gint invalidate_y;
volatile gint invalidate_w;
volatile gint invalidate_h;


static void mouse_wrap(SpiceDisplay *display, GlueMotionEvent *motion)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    gint xr, yr;

#ifdef WIN32
    SPICE_DEBUG("%s pointer: sending cursor to the middle of the clip area", __FUNCTION__);
    /* WORKAROUND
     * Theoretically the cursor is clipped in do_pointer_grab(),
     * and it does not get unclipped until do_pointer_ungrab() is called.
     * Windows docs say that after ClipCursor(rect), the cursor is clipped
     * until ClipCursor(NULL is called).
     * BUT what REALLY happens is that AFTER clipping the cursor, it gets
     * unclipped again WITHOUT a call to ClipCursor(NULL)
     * This made that mouse_wrap moved the cursor to the center of the
     * desktop, which can be outside the spice client program rendering
     * the mouse unusable (only after clicking on the top bar, and only in
     * mouse-server-mode)
     * We work around what seems a windows bug calling win32_clip_cursor again.
     **/
    win32_clip_cursor(); // Workaround

    RECT clip;
    g_return_if_fail(GetClipCursor(&clip));
    //SPICE_DEBUG("%s pointer: GetClipCursor() clip: t:%ld, b:%ld, l:%ld, r:%ld, ", __FUNCTION__,
    //		clip.top, clip.bottom, clip.left, clip.right);
    xr = clip.left + (clip.right - clip.left) / 2;
    yr = clip.top + (clip.bottom - clip.top) / 2;
    /* the clip rectangle has no offset, so we can't use gdk_wrap_pointer */
    if (d->have_focus) {// If not focused, do not move the pointer
	SPICE_DEBUG(" %s pointer SetCursorPos ot %d, %d", __FUNCTION__, xr, yr);

	SetCursorPos(xr, yr);
	d->mouse_last_x = -1;
	d->mouse_last_y = -1;
    }
#else
    SPICE_DEBUG("%s not implemented", __FUNCTION__);
#endif
}

// Leave the pointer free of the window "jail" set by try_mouse_grab.
static void try_mouse_ungrab(SpiceDisplay *display)
{
    SPICE_DEBUG("%s", __FUNCTION__);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (!d->mouse_grab_active)
	return;

    //gdk_pointer_ungrab(GDK_CURRENT_TIME);
    //gtk_grab_remove(GTK_WIDGET(display));
#ifdef WIN32
    ClipCursor(NULL);
    SPICE_DEBUG("ClipCursor(NULL)");
#endif
    set_mouse_accel(display, TRUE);

    d->mouse_grab_active = false;

    g_signal_emit(display, signals[SPICE_DISPLAY_MOUSE_GRAB], 0, false);
}

static int button_mono_to_spice(int buttonId)
{
    static const int map[] = {
	[ 1 ] = SPICE_MOUSE_BUTTON_LEFT,
	[ 2 ] = SPICE_MOUSE_BUTTON_MIDDLE,
	[ 3 ] = SPICE_MOUSE_BUTTON_RIGHT,
	[ 4 ] = SPICE_MOUSE_BUTTON_UP,
	[ 5 ] = SPICE_MOUSE_BUTTON_DOWN,
    };

    if (buttonId < SPICE_N_ELEMENTS(map)) {
	return map [ buttonId ];
    }
    return 0;
}

inline static int button_mask_monoglue_to_spice(int mono)
{
    return mono;
}

G_GNUC_INTERNAL
void spicex_transform_input (SpiceDisplay *display,
                             double window_x, double window_y,
                             int *input_x, int *input_y)
{
    *input_x = floor (window_x);
    *input_y = floor (window_y);
}

int16_t SpiceGlibGlueButtonEvent(int32_t eventX, int32_t eventY,
				 int16_t buttonId, int16_t buttonState, int16_t isDown)
{
    if (global_display == NULL) {
	return -1;
    }
    SpiceDisplay* display = global_display;

    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->data == NULL) {
	return -1;
    }

    int x, y;

    SPICE_DEBUG("%s %s: button %d x: %d, y: %d, state: %d", __FUNCTION__,
		isDown ? "press" : "release",
		buttonId, eventX, eventY, buttonState);

    if (d->disable_inputs)
	return true;

    spicex_transform_input (display, eventX, eventY, &x, &y);
    
    //gtk_widget_grab_focus(widget);
    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
	if (!d->mouse_grab_active) {
	    try_mouse_grab(display);
	    SPICE_DEBUG("%s Evento dedicado a tratar de grab el raton no hacemos click", __FUNCTION__);
	    return true;
	}
    } else {
	/* allow to drag and drop between windows/displays:

	   By default, X (and other window system) do a pointer grab
	   when you press a button, so that the release event is
	   received by the same window regardless of where the pointer
	   is. Here, we change that behaviour, so that you can press
	   and release in two differents displays. This is only
	   supported in client mouse mode.

	   FIXME: should be multiple widget grab, but how?
	   or should know the position of the other widgets?
	*/
	//  gdk_pointer_ungrab(GDK_CURRENT_TIME);
    }

    if (!d->inputs)
	return true;

    if (isDown) {
	spice_inputs_button_press(d->inputs,
				  button_mono_to_spice(buttonId),
				  button_mask_monoglue_to_spice(buttonState));
    } else {
	spice_inputs_button_release(d->inputs,
				    button_mono_to_spice(buttonId),
				    button_mask_monoglue_to_spice(buttonState));
    }
    return true;
}

int16_t SpiceGlibGlueMotionEvent(int32_t eventX, int32_t eventY,
				 int16_t buttonState)
{
    //SPICE_DEBUG("%s: pointer  x: %d, y: %d, state: %d", __FUNCTION__, eventX, eventY, buttonState);
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    GlueMotionEvent event;
    int x, y;

    if (global_display == NULL) {
	return -1;
    }

    display = global_display;
    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->data == NULL) {
	return -1;
    }

    event.x = eventX;
    event.y = eventY;
    event.buttonState = buttonState;

    if (!d->inputs)
	return true;
    if (d->disable_inputs)
	return true;

    spicex_transform_input (display, eventX, eventY, &x, &y);

    //SPICE_DEBUG("%s: pointer spicex_transform_input x: %d, y: %d", __FUNCTION__, x, y);

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
	if (x >= 0 && /*x < d->area.width &&*/
	    y >= 0 /*&& y < d->area.height*/) {
	    spice_inputs_position(d->inputs, x, y, get_display_id(display),
				  button_mask_monoglue_to_spice(buttonState));
	}
	break;
    case SPICE_MOUSE_MODE_SERVER:
	//SPICE_DEBUG("%s:  pointer SERVER MODE mouse_grab_active==%d", __FUNCTION__, d->mouse_grab_active);
	if (d->have_focus) {

	    //SPICE_DEBUG("%s:  if mouse_grab_active PUENTEADO", __FUNCTION__);
	    gint dx = d->mouse_last_x != -1 ? x - d->mouse_last_x : 0;
	    gint dy = d->mouse_last_y != -1 ? y - d->mouse_last_y : 0;
	    //SPICE_DEBUG("%s: pointer Pasando motion: dx %d, dy %d ", __FUNCTION__, dx, dy);


	    spice_inputs_motion(d->inputs, dx, dy,
				button_mask_monoglue_to_spice(buttonState));

	    d->mouse_last_x = x;
	    d->mouse_last_y = y;

	    if (dx != 0 || dy != 0)
		mouse_wrap(display, &event);

	}
	else {
	    //SPICE_DEBUG("%s: pointer SERVER MODE AND NOT FOCUS ---> ignorando eventos ****", __FUNCTION__);
	}
	break;

    default:
	g_warn_if_reached();
	break;
    }
    return 0;
}

static void update_keyboard_focus(SpiceDisplay *display, gboolean state)
{
    SPICE_DEBUG("%s", __FUNCTION__);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    d->have_focus = state;

    /* keyboard grab gets inhibited by usb-device-manager when it is
       in the process of redirecting a usb-device (as this may show a
       policykit dialog). Making autoredir/automount setting changes while
       this is happening is not a good idea! */
    if (d->keyboard_grab_inhibit)
	return;

    //spice_gtk_session_request_auto_usbredir(d->gtk_session, state);
}

int16_t SpiceGlibGlueOnGainFocus()
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;

    SPICE_DEBUG("%s", __FUNCTION__);
    if (global_display == NULL) {
	SPICE_DEBUG("%s ERROR pointer global_display == NULL", __FUNCTION__);
	return -1;
    }

    display = global_display;
    d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d == NULL) {
	SPICE_DEBUG("%s ERROR pointer d->data == NULL", __FUNCTION__);
	return -1;
    }

#ifdef WIN32
    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
	if (!win32_clip_cursor()) {
	    SPICE_DEBUG("%s ERROR win32_clip_cursor failed", __FUNCTION__);
	    //	return -1;
	}
    }
    //return 0;
#endif

    /*
     * Ignore focus in when we already have the focus
     * (this happens when doing an ungrab from the leave_event callback).
     */
    if (d->have_focus) {
	SPICE_DEBUG("%s have_focus==true not setting again. NOT setting focus again", __FUNCTION__);
	return true;
    }

    // We released the keys when we lost focus, so this should do nothing now.
    release_keys(display);
    sync_keyboard_lock_modifiers(display);
    update_keyboard_focus(display, true);
    // TODO call this again...
    // FIXME
    //try_keyboard_grab(display);
    // We will need this when we use draw-invalidadted-area:
    //update_display(display); // Continuously update full display now...

    return -1;
}

int16_t SpiceGlibGlueOnLoseFocus()
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;

    SPICE_DEBUG("%s", __FUNCTION__);
    if (global_display == NULL) {
	return -1;
    }

    display = global_display;
    d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->data == NULL) {
	return -1;
    }

    /*
     * Ignore focus out after a keyboard grab
     * (this happens when doing the grab from the enter_event callback).
     */
    if (d->keyboard_grab_active)
	return true;


    release_keys(display);
    update_keyboard_focus(display, false);

#ifdef WIN32
    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
	ClipCursor(NULL);
	SPICE_DEBUG("ClipCursor(NULL)");
    }
    return 0;
#endif
    return -1;
}


int16_t SpiceGlibGlueScrollEvent(int16_t buttonState, int16_t isDown)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    int button;

    if (global_display == NULL) {
	return -1;
    }

    if (d->data == NULL) {
	return -1;
    }

    SPICE_DEBUG("%s", __FUNCTION__);

    if (!d->inputs)
	return true;
    if (d->disable_inputs)
	return true;

    if (!isDown)
	button = SPICE_MOUSE_BUTTON_UP;
    else
	button = SPICE_MOUSE_BUTTON_DOWN;

    spice_inputs_button_press(d->inputs, button,
			      button_mask_monoglue_to_spice(buttonState));
    spice_inputs_button_release(d->inputs, button,
				button_mask_monoglue_to_spice(buttonState));
    return true;
}

static void primary_create(SpiceChannel *channel,
			   gint format, gint width, gint height, gint stride,
			   gint shmid, gpointer imgdata, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    d->format = format;
    d->stride = stride;
    d->shmid = shmid;
    d->width = width;
    d->height = height;
    d->data_origin = d->data = imgdata;

    update_monitor_area(display);
}

static void primary_destroy(SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = SPICE_DISPLAY(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    //spicex_image_destroy(display);
    d->format = 0;
    d->width  = 0;
    d->height = 0;
    d->stride = 0;
    d->shmid  = 0;
    d->data   = NULL;
    d->data_origin = NULL;
}

extern uint32_t *glue_display_buffer;
extern gboolean updatedDisplayBuffer;

extern STATIC_MUTEX glue_display_lock;
extern int32_t glue_width;
extern int32_t glue_height;
extern int32_t local_width;
extern int32_t local_height;
typedef unsigned int Color32;

static inline Color32 ARGBtoABGR(Color32 x)
{
    return (( 0xFF000000) ) | 
	((x & 0x00FF0000) >> 16) | 
	((x & 0x0000FF00) ) | 
	((x & 0x000000FF) <<  16 );
}

gint64 last_copy_timestamp = 0;
volatile int copy_scheduled = 0;

gboolean copy_display_to_glue(SpiceDisplayPrivate *d)
{
    gint64 now_timestamp = g_get_monotonic_time();
    gint64 delta = (now_timestamp - last_copy_timestamp);

    /* Limit copy_display_to_glue to 1000/30 == 33hz */
    if (delta < 30000) {
	// SPICE_DEBUG("omitting early copy");
	return TRUE;
    }

    if (d->data == NULL || d->width == 0 || d->height == 0) {
	SPICE_DEBUG("local display is not available");
	return TRUE;
    }

    if (local_width != d->width || local_height != d->height) {
	SPICE_DEBUG("local dimensions changed since scheduled\n");
	return TRUE;
    }

    if (glue_width < local_width || glue_height < local_height) {
	SPICE_DEBUG("glue display dimensions are too small");
	return TRUE;
    }

    if (glue_display_buffer == NULL) {
        SPICE_DEBUG("glue_display_buffer is not initialized yet");
        return TRUE;
    }

    STATIC_MUTEX_LOCK(glue_display_lock);
    Color32 * src2_data = (Color32 *)d->data;
    Color32 * dst2_data = (Color32 *)glue_display_buffer;
    int maxI = d->height > (invalidate_y + invalidate_h)? d->height - invalidate_y : invalidate_h;
    int maxJ = d->width  > (invalidate_x + invalidate_w)? d->width :(invalidate_x + invalidate_w);
    src2_data += d->width * invalidate_y;
#if defined(__APPLE__) || defined(ANDROID)
    int tmp = (d->height - invalidate_y - 1) * d->width;
    dst2_data += tmp;
#else
    dst2_data += d->width * invalidate_y;
#endif

    int i, j;
    for (i = 0 ; i < maxI; i++) {
	for (j = invalidate_x; j < maxJ; j ++) {
	    dst2_data[j]=ARGBtoABGR(src2_data[j]);
	}
#if defined(__APPLE__) || defined(ANDROID)
	dst2_data-= d->width;
#else
	dst2_data+= d->width;
#endif
	src2_data += d->width;
    }

    last_copy_timestamp= now_timestamp;
    copy_scheduled = 0;
    invalidated = FALSE;
    updatedDisplayBuffer = TRUE;


    STATIC_MUTEX_UNLOCK(glue_display_lock);
    return FALSE;
}

/* Called when we receive a new display image.
 * Sets invalidated = TRUE, and updates the values of invalidate_x/y/w/h that
 * store the coordinates of the area to copy_display_to_glue().
 *
 * We don't know if display_glue has been modified. We don't care.
 * */
static void invalidate(SpiceChannel *channel,
                       gint x, gint y, gint w, gint h, gpointer data)
{
    SpiceDisplay *display = SPICE_DISPLAY(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(global_display);
    char *cdata = (char *)d->data;
    
    if (invalidated == TRUE) {
	/*SPICE_DEBUG("*** 0000 PRE inval x: %d, w: %d, y: %d, h: %d",
	  invalidate_x, invalidate_w, invalidate_y, invalidate_h );
	  SPICE_DEBUG("*** **** PRE nuevo x: %d, w: %d, y: %d, h: %d",
	  x, w, y, h );*/
	if (local_width != d->width ||
	    local_height != d->height) {
	    invalidate_x = 0;
	    invalidate_y = 0;
	    invalidate_w = d->width;
	    invalidate_h = d->height;
	    local_width = d->width;
	    local_height = d->height;
	} else {
	    gint invalidate_x0 = invalidate_x;
	    gint invalidate_y0 = invalidate_y;
	    if (x < invalidate_x)
		invalidate_x = x;
	    if (y < invalidate_y)
		invalidate_y = y;
	    if ((x + w) > (invalidate_x0 + invalidate_w))
		invalidate_w = (x + w) - invalidate_x;
	    if ((y + h) > (invalidate_y0 + invalidate_h))
		invalidate_h = (y + h) - invalidate_y;
	}

	if (glue_display_buffer == NULL) {
	    SPICE_DEBUG("glue_display_buffer not yet initialized");
	    return;
	}

	/*SPICE_DEBUG("*** **** POST inval x: %d, w: %d, y: %d, h: %d, &x: %xd",
	  invalidate_x, invalidate_w, invalidate_y, invalidate_h, &invalidate_x );*/
    } else {
	invalidated = TRUE;
	invalidate_x = x;
	invalidate_y = y;
	invalidate_w = w;
	invalidate_h = h;
	local_width = d->width;
	local_height = d->height;
    }

    if (!copy_scheduled) {
	g_idle_add((GSourceFunc) copy_display_to_glue, (gpointer) d);
	copy_scheduled = 1;
    }
}

static void update_ready(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    gboolean ready;

    ready = d->mark != 0 && d->monitor_ready;

    if (d->ready == ready)
	return;

    if (ready /*&& gtk_widget_get_window(GTK_WIDGET(display)))
		gtk_widget_queue_draw(GTK_WIDGET(display)*/);

    d->ready = ready;
    g_object_notify(G_OBJECT(display), "ready");
}

static void mark(SpiceDisplay *display, gint mark)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    g_return_if_fail(d != NULL); //TODO: Ojo; esta se dispara con frecuencia

    SPICE_DEBUG("widget mark: %d, %d:%d %p", mark, d->channel_id, d->monitor_id, display);
    d->mark = mark;
    update_ready(display);
}

/* Cursor struct for Mono client */
typedef struct _SpiceGlibGlueCursorData

{
    uint32_t width;
    uint32_t height;
    uint32_t hot_x;
    uint32_t hot_y;
    //int32_t* rgba;
} SpiceGlibGlueCursorData;

MonoGlueCursor* monoglue_cursor_new_from_data(uint32_t width, uint32_t height, uint32_t hot_x, uint32_t hot_y, uint32_t* rgba) {

    MonoGlueCursor* ret = g_new(MonoGlueCursor, 1);
    ret->width=  width;
    ret->height= height;
    ret->hot_x=  hot_x;
    ret->hot_y=  hot_y;
    ret->rgba= g_memdup(rgba, width * height * 4);

    return ret;
}

MonoGlueCursor* get_blank_cursor() {

    uint32_t imagen[1]={0};
    return monoglue_cursor_new_from_data(1, 1, 0, 0, imagen);
}

void monoglue_cursor_finalize(MonoGlueCursor* c) {
    if (c) {
	if (c->rgba) {
	    g_free(c->rgba);
	    c->rgba= NULL;
	}
	g_free(c);
    }
}

static void cursor_set(SpiceCursorChannel *channel,
                       gint width, gint height, gint hot_x, gint hot_y,
                       gpointer rgba, gpointer data)
{
    /*SPICE_DEBUG("pointer %s %s: button %d x: %d, y: %d, state: %d", __FUNCTION__,
      isDown ? "press" : "release",
      buttonId, eventX, eventY, buttonState);*/
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    MonoGlueCursor *cursor = NULL;

    STATIC_MUTEX_LOCK(d->cursor_lock);

    if (rgba != NULL) {
	cursor= monoglue_cursor_new_from_data(width, height, hot_x, hot_y, rgba);
    } else
	g_warn_if_reached();

    if (d->show_cursor) {
	monoglue_cursor_finalize(d->show_cursor);
	d->show_cursor = NULL;
	if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
	    SPICE_DEBUG("%s pointer copying cursor image to show_cursor:", __FUNCTION__);
	    /* keep a hidden cursor, will be shown in cursor_move() */
	    d->show_cursor= monoglue_cursor_new_from_data(width, height, hot_x, hot_y, rgba);
	    goto end;
	}
    }

    monoglue_cursor_finalize(d->mouse_cursor);

    d->mouse_cursor = cursor;
    SPICE_DEBUG("%s : w: %d, h: %d, v[0] %04x", __FUNCTION__,
		cursor->width, cursor->height, cursor->rgba[0]);

    (d->idCursor)++;

 end:
    STATIC_MUTEX_UNLOCK(d->cursor_lock);
}

static void cursor_move(SpiceCursorChannel *channel, gint x, gint y, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    SPICE_DEBUG("%s: x %d, y %d d->mouse_guest_x: %d d->mouse_guest_y: %d ",
		__FUNCTION__, x, y, d->mouse_guest_x, d->mouse_guest_y);


    STATIC_MUTEX_LOCK(d->cursor_lock);

    /* In server mode, we receive mouse location via cursor-channel */
    d->mouse_guest_x = x;
    d->mouse_guest_y = y;

    /* apparently we have to restore cursor when "cursor_move" */
    if (d->show_cursor != NULL) {
	//gdk_cursor_unref(d->mouse_cursor);
	if (d->mouse_cursor) {
	    monoglue_cursor_finalize(d->mouse_cursor);
	    d->mouse_cursor=NULL;
	}
	d->mouse_cursor = d->show_cursor;
	d->show_cursor = NULL;
	//SPICE_DEBUG("%s not update_mouse_pointer",  __FUNCTION__);
    }
    SPICE_DEBUG("%s exiting critical section",  __FUNCTION__);

    STATIC_MUTEX_UNLOCK(d->cursor_lock);
}

static void cursor_hide(SpiceCursorChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    STATIC_MUTEX_LOCK(d->cursor_lock);
    SPICE_DEBUG("cursor_hide()");

    if (d->show_cursor != NULL) /* then we are already hidden */
	goto end;

    //cursor_invalidate(display);
    d->show_cursor = d->mouse_cursor;
    d->mouse_cursor = get_blank_cursor();
 end:
    STATIC_MUTEX_UNLOCK(d->cursor_lock);
}

static void cursor_reset(SpiceCursorChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    STATIC_MUTEX_LOCK(d->cursor_lock);
    SPICE_DEBUG("%s",  __FUNCTION__);

    if (d->mouse_cursor) {
	monoglue_cursor_finalize(d->mouse_cursor);
	d->mouse_cursor = NULL;
    }
    if (d->show_cursor) {
	monoglue_cursor_finalize(d->show_cursor);
	d->show_cursor = NULL;
    }

    STATIC_MUTEX_UNLOCK(d->cursor_lock);
    //gdk_window_set_cursor(window, NULL);
}

int16_t SpiceGlibGlueGetCursor(uint32_t previousCursorId,
			       uint32_t* currentCursorId,
			       uint32_t* showInClient,
			       SpiceGlibGlueCursorData* cursor,
			       int32_t* dstRgba)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;

    if (global_display == NULL) {
	return -1;
    }

    display = global_display;
    d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->data == NULL) {
	return -1;
    }

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
	*showInClient=true;
	break;
    case SPICE_MOUSE_MODE_SERVER:
	*showInClient=false;
	break;
    default:
	g_warn_if_reached();
	break;
    }

    STATIC_MUTEX_LOCK(d->cursor_lock);

    if (previousCursorId!=d->idCursor) {
	SPICE_DEBUG("%s : Changing cursor ", __FUNCTION__);
	MonoGlueCursor* mgc = d->mouse_cursor;
	if (mgc) {
	    cursor->width= mgc->width;
	    cursor->height= mgc->height;
	    cursor->hot_x= mgc->hot_x;
	    cursor->hot_y= mgc->hot_y;
	    //SPICE_DEBUG("%s : ..Read h: %d w: %d ....", __FUNCTION__, mgc->height, mgc->width);
	    /*SPICE_DEBUG("%s : ..Read value0: %d value1: %d rgba-address: %p ..", __FUNCTION__,
	      cursor->rgba[0], cursor->rgba[1], cursor->rgba);
	      dstrgba[0], dstrgba[1], dstrgba);*/
	    memcpy( dstRgba, mgc->rgba, mgc->width * mgc->height *sizeof(*dstRgba));
	}
    }

    STATIC_MUTEX_UNLOCK(d->cursor_lock);
    *currentCursorId = d->idCursor;
    return 0;
}

static void disconnect_main(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->main == NULL)
	return;
    d->main = NULL;
}

static void disconnect_display(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->display == NULL)
	return;

    primary_destroy(d->display, display);

    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(primary_create),
					 display);
    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(primary_destroy),
					 display);
    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(invalidate),
					 display);
    d->display = NULL;
}

static void disconnect_cursor(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->cursor == NULL)
	return;

    //cursor_destroy(d->display, display); eh?

    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(cursor_set),
					 display);
    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(cursor_move),
					 display);
    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(cursor_hide),
					 display);
    g_signal_handlers_disconnect_by_func(d->display, G_CALLBACK(cursor_reset),
					 display);
    d->display = NULL;
}


static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    //SPICE_DEBUG(" ***** spice-widget -> channel_new %p ", channel);

    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    SPICE_DEBUG("channel id: %d ", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
	//SPICE_DEBUG(" channel_new: MAIN_CHANEL del display %d ", get_display_id(display));
	d->main = SPICE_MAIN_CHANNEL(channel);
	spice_g_signal_connect_object(channel, "main-mouse-update",
				      G_CALLBACK(update_mouse_mode), display, 0);
	update_mouse_mode(channel, display);
	if (d->display) {
	    spice_main_set_display_enabled(d->main, get_display_id(display), TRUE);
	}

	return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
	//SPICE_DEBUG(" ***** channel_new: ES DISPLAY_CHANEL del display %d ", get_display_id(display));
	SpiceDisplayPrimary primary;
	if (id != d->channel_id)
	    return;
	d->display = channel;
	g_signal_connect(channel, "display-primary-create",
			 G_CALLBACK(primary_create), display);
	g_signal_connect(channel, "display-primary-destroy",
			 G_CALLBACK(primary_destroy), display);
	g_signal_connect(channel, "display-invalidate",
			 G_CALLBACK(invalidate), display);
	spice_g_signal_connect_object(channel, "display-mark",
				      G_CALLBACK(mark), display, G_CONNECT_AFTER | G_CONNECT_SWAPPED);
	spice_g_signal_connect_object(channel, "notify::monitors",
				      G_CALLBACK(update_monitor_area), display, G_CONNECT_AFTER | G_CONNECT_SWAPPED);

	if (spice_display_get_primary(channel, 0, &primary)) {
	    primary_create(channel, primary.format, primary.width, primary.height,
			   primary.stride, primary.shmid, primary.data, display);
	    mark(display, primary.marked);
	}
	spice_channel_connect(channel);
	spice_main_set_display_enabled(d->main, get_display_id(display), TRUE);
	return;
    }

    if (SPICE_IS_CURSOR_CHANNEL(channel)) {
	//SPICE_DEBUG(" ***** channel_new: ES CURSOR_CHANEL del display %d ", get_display_id(display));

	if (id != d->channel_id)
	    return;
	d->cursor = SPICE_CURSOR_CHANNEL(channel);
	g_signal_connect(channel, "cursor-set",
			 G_CALLBACK(cursor_set), display);
	g_signal_connect(channel, "cursor-move",
			 G_CALLBACK(cursor_move), display);
	g_signal_connect(channel, "cursor-hide",
			 G_CALLBACK(cursor_hide), display);
	g_signal_connect(channel, "cursor-reset",
			 G_CALLBACK(cursor_reset), display);
	spice_channel_connect(channel);
	return;
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
	//SPICE_DEBUG(" ***** channel_new: ES INPUTS_CHANEL del display %d ", get_display_id(display));
	d->inputs = SPICE_INPUTS_CHANNEL(channel);
	spice_channel_connect(channel);
	sync_keyboard_lock_modifiers(display);
	return;
    }

#ifdef USE_SMARTCARD
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
	d->smartcard = SPICE_SMARTCARD_CHANNEL(channel);
	spice_channel_connect(channel);
	return;
    }
#endif
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
	//SPICE_DEBUG(" ***** channel_new: ES SMARTCARD_CHANEL del display %d ", get_display_id(display));
    }
    //SPICE_DEBUG(" ***** END -spice-widget -> channel_new ");
    return;
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    SPICE_DEBUG("channel_destroy %d", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
	disconnect_main(display);
	return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
	if (id != d->channel_id)
	    return;
	disconnect_display(display);
	return;
    }

    if (SPICE_IS_CURSOR_CHANNEL(channel)) {
	if (id != d->channel_id)
	    return;
	disconnect_cursor(display);
	return;
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
	d->inputs = NULL;
	return;
    }

#ifdef USE_SMARTCARD
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
	d->smartcard = NULL;
	return;
    }
#endif

    return;
}

/**
 * spice_display_new:
 * @session: a #SpiceSession
 * @id: the display channel ID to associate with #SpiceDisplay
 *
 * Returns: a new #SpiceDisplay widget.
 **/
SpiceDisplay *spice_display_new(SpiceSession *session, int id)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    GList *list;
    GList *it;

    display = g_object_new(SPICE_TYPE_DISPLAY, NULL);
    d = SPICE_DISPLAY_GET_PRIVATE(display);
    d->session = g_object_ref(session);
    d->channel_id = id;
    SPICE_DEBUG("channel_id:%d",d->channel_id);

    g_signal_connect(session, "channel-new",
		     G_CALLBACK(channel_new), display);
    g_signal_connect(session, "channel-destroy",
		     G_CALLBACK(channel_destroy), display);
    list = spice_session_get_channels(session);
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
	channel_new(session, it->data, (gpointer*)display);
    }
    g_list_free(list);



    return display;
}

#if HAVE_X11_XKBLIB_H
static guint32 get_keyboard_lock_modifiers(Display *x_display)
{
    XKeyboardState keyboard_state;
    guint32 modifiers = 0;

    XGetKeyboardControl(x_display, &keyboard_state);

    if (keyboard_state.led_mask & 0x01) {
	modifiers |= SPICE_INPUTS_CAPS_LOCK;
    }
    if (keyboard_state.led_mask & 0x02) {
	modifiers |= SPICE_INPUTS_NUM_LOCK;
    }
    if (keyboard_state.led_mask & 0x04) {
	modifiers |= SPICE_INPUTS_SCROLL_LOCK;
    }
    return modifiers;
}

static void sync_keyboard_lock_modifiers(SpiceDisplay *display)
{
    Display *x_display;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    guint32 modifiers;
    GdkWindow *w;

    if (d->disable_inputs)
	return;

    w = gtk_widget_get_parent_window(GTK_WIDGET(display));
    if (w == NULL) /* it can happen if the display is not yet shown */
	return;

    if (!GDK_IS_X11_DISPLAY(gdk_window_get_display(w))) {
	SPICE_DEBUG("FIXME: gtk backend is not X11");
	return;
    }

    x_display = GDK_WINDOW_XDISPLAY(w);
    modifiers = get_keyboard_lock_modifiers(x_display);
    if (d->inputs)
	spice_inputs_set_key_locks(d->inputs, modifiers);
}

#elif defined (WIN32)
static guint32 get_keyboard_lock_modifiers(void)
{
    guint32 modifiers = 0;

    if (GetKeyState(VK_CAPITAL) & 1) {
	modifiers |= SPICE_INPUTS_CAPS_LOCK;
    }
    if (GetKeyState(VK_NUMLOCK) & 1) {
	modifiers |= SPICE_INPUTS_NUM_LOCK;
    }
    if (GetKeyState(VK_SCROLL) & 1) {
	modifiers |= SPICE_INPUTS_SCROLL_LOCK;
    }

    return modifiers;
}

static void sync_keyboard_lock_modifiers(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    guint32 modifiers;

    if (d->disable_inputs)
	return;

    modifiers = get_keyboard_lock_modifiers();
    if (d->inputs)
	spice_inputs_set_key_locks(d->inputs, modifiers);
}
#else
static void sync_keyboard_lock_modifiers(SpiceDisplay *display)
{
    g_warning("sync_keyboard_lock_modifiers not implemented");
}
#endif // HAVE_X11_XKBLIB_H
