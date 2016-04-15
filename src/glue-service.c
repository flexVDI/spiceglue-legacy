/**
 * Copyright (C) 2016 flexVDI (Flexible Software Solutions S.L.)
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

#define GLUE_SERVICE_C

#include <stdbool.h>

#ifdef ANDROID
#include <android/log.h>
#endif

#include "glue-service.h"

#include "glue-spice-widget.h"
#include "glue-spice-widget-priv.h"
#include "glue-spicy.h"

#include "glib.h"
#if defined(PRINTING) || defined(SSO)
#include "flexvdi-port.h"
#endif
#ifdef PRINTING
#include "glue-printing.h"
#endif
#ifdef SSO
#include "glue-sso.h"
#endif

static FILE *logfile = NULL;

void logToFile (const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data)
{
    GDateTime *now;
    gchar *dateTimeStr;
    
    if (logfile == NULL) {
	const gchar *basePath = g_getenv("FLEXVDICLIENT_LOGDIR");
	gchar path[4096];

	sprintf(path, "%sflexVDIClient-lib.log", basePath);
	logfile = fopen (path, "a");

	if (logfile == NULL) {
	    fprintf (stderr, "Rerouted to console: %s\n", message);
	    return;
	}
    }

    now = g_date_time_new_now_local();
    dateTimeStr = g_date_time_format(now, "%Y-%m-%d %T");

    fprintf (logfile, "%s,%03d %s-%s\n", dateTimeStr,
	     g_date_time_get_microsecond(now) / 1000, log_domain, message);

    g_date_time_unref(now);
    g_free(dateTimeStr);
    fflush(logfile);
}

void nullLog (const gchar *log_domain, GLogLevelFlags log_level,
	      const gchar *message, gpointer user_data)
{
}

#ifdef ANDROID
void androidLog (const gchar *log_domain, GLogLevelFlags log_level,
		 const gchar *message, gpointer user_data) {
    __android_log_print(ANDROID_LOG_ERROR, "spice-glue", "%s", message);
}
#endif


void SpiceGlibGlue_InitializeLogging(int32_t verbosityLevel)
{
    SPICE_DEBUG("SpiceGlibGlue_InitializeLogging() ini");

    GLogLevelFlags logFlags = G_LOG_LEVEL_MASK;

    switch (verbosityLevel) {
    case 0:
	logFlags = G_LOG_LEVEL_ERROR;
	break;
    case 1:
	logFlags = G_LOG_LEVEL_ERROR |
	  G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING;
	break;
    case 2:
	logFlags = G_LOG_LEVEL_ERROR |
	  G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE;
	break;
    case 3:
	logFlags = G_LOG_LEVEL_MASK; /* all levels*/
	spice_util_set_debug(TRUE);
    }

    /* 
     * Cancel out defaul log to stderr, and stdout.
     * Otherwise our custom logger AND default logger will log.
     * Only installing logToFile does NOT nullify default log.
     */
    g_log_set_default_handler (nullLog, NULL);

#ifdef ANDROID
    // glue library (SPICE_DEBUG)
    g_log_set_handler ("SpiceGlue",  logFlags, androidLog, NULL);
    // Some logs with "" domain in sourcesa as glue-spicy.c
    g_log_set_handler ("",  logFlags, androidLog, NULL);
    // libspiceClient Logs (SPICE_DEBUG)
    g_log_set_handler ("GSpice",  logFlags, androidLog, NULL);
    // flexvdi library logging. Follow Me Printing, etc
    g_log_set_handler ("flexvdi",  logFlags, androidLog, NULL);
#else
    // glue library (SPICE_DEBUG)
    g_log_set_handler ("SpiceGlue",  logFlags, logToFile, NULL);
    // Some logs with "" domain in sourcesa as glue-spicy.c
    g_log_set_handler ("",  logFlags, logToFile, NULL);
    // libspiceClient Logs (SPICE_DEBUG)
    g_log_set_handler ("GSpice",  logFlags, logToFile, NULL);
    // flexvdi library logging. Follow Me Printing, etc
    g_log_set_handler ("flexvdi",  logFlags, logToFile, NULL);
#endif

    SPICE_DEBUG("Logging initialized.");
}

spice_connection *mainconn;

void SpiceGlibGlue_MainLoop(void)
{
    mainloop = g_main_loop_new(NULL, false);
    g_main_loop_run(mainloop);
}

void SpiceGlibGlue_Disconnect(void)
{
    SPICE_DEBUG("SpiceGlibGlue_Disconnect\n");
    connection_disconnect(mainconn);
    global_disconnecting = 1;
}

int16_t SpiceGlibGlue_Connect(char* host,
			      char* port, char* tls_port, char* ws_port,
			      char* password,
			      char* ca_file, char* cert_subj,
			      int32_t enable_sound)
{
    int result = 0;
    global_disconnecting = 0;
    soundEnabled = enable_sound;

    SPICE_DEBUG("SpiceClientConnect session_setup");

    mainconn = connection_new();
    spice_session_setup(mainconn->session, host,
			port, tls_port, ws_port,
			password,
			ca_file, cert_subj);

#if defined(PRINTING) || defined(SSO)
    flexvdi_port_register_session(mainconn->session);
#endif
#if defined(PRINTING)
    onConnectGuestFollowMePrinting();
#endif

    SPICE_DEBUG("SpiceClientConnect connection_connect");

    connection_connect(mainconn);
    if (connections < 0) {
    	SPICE_DEBUG("Wrong hostname, port, or password.");
        result = 2;
    }

    SPICE_DEBUG("SpiceClientConnect exit");

    return result;
}

extern volatile gboolean invalidated;
extern volatile gint invalidate_x;
extern volatile gint invalidate_y;
extern volatile gint invalidate_w;
extern volatile gint invalidate_h;

typedef unsigned int Color32;
static inline Color32 ARGBtoABGR(Color32 x)
{
    return 0xFF000000 |              // ______AA
	((x & 0x00FF0000) >> 16) |   // ____RR__
        (x & 0x0000FF00) |           // __GG____
        ((x & 0x000000FF) <<  16 );  // BB______
}

uint32_t *glue_display_buffer = NULL; 
gboolean updatedDisplayBuffer = FALSE;

/* MUTEX to ensure that glue_display_buffer is not freed while it's being written */
STATIC_MUTEX  glue_display_lock;

/* Size of the image stored in glue_display_buffer */
int32_t glue_width = 0;
int32_t glue_height = 0;

/* Copy of the size of d->data the last time invalidate() was called */
int32_t local_width = 0;
int32_t local_height = 0;

void SpiceGlibGlueInitializeGlue()
{
#ifdef PRINTING
    initializeFollowMePrinting();
#endif
#ifdef SSO
    initializeSSO();
#endif
    STATIC_MUTEX_INIT(glue_display_lock);
}


void SpiceGlibGlueSetDisplayBuffer(uint32_t *display_buffer,
				   int32_t width, int32_t height)
{
    SPICE_DEBUG("SpiceGlibGlueSetDisplayBuffer");

    glue_display_buffer = display_buffer;
    glue_width = width;
    glue_height = height;
}

/** 
 * Locks the glue_display_buffer, so that we can safely call
 * SpiceGlibGlueSetDisplayBuffer()
 * Params: *width, *height
 *  IN: don't care
 *  OUT: size of display used by the spice-client-lib: Real guest display, and what the
 * Returns true if current buffer has changed and has not been copied, false otherwise.
 **/
int16_t SpiceGlibGlueLockDisplayBuffer(int32_t *width, int32_t *height)
{
    SPICE_DEBUG("SpiceGlibGlueLockDisplayBuffer");

    STATIC_MUTEX_LOCK(glue_display_lock);

    *width = local_width;
    *height = local_height;

    if (updatedDisplayBuffer) {
    	updatedDisplayBuffer = FALSE;
    	return 1;
    }
    return 0;
}

void SpiceGlibGlueUnlockDisplayBuffer()
{
    SPICE_DEBUG("SpiceGlibGlueUnlockDisplayBuffer");

    STATIC_MUTEX_UNLOCK(glue_display_lock);
}

int16_t SpiceGlibGlueGetCursorPosition(int32_t* x, int32_t* y)
{
    SpiceDisplayPrivate *d;

    if (global_display == NULL) {
	return -1;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(global_display);

    if (d->data == NULL) {
	//SPICE_DEBUG("d->data == NULL");
	return -1;
    }

    *x=d->mouse_guest_x;
    *y=d->mouse_guest_y;

    return 0;
}

int32_t SpiceGlibGlue_SpiceKeyEvent(int16_t isDown, int32_t hardware_keycode)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    int scancode;
    
    display = global_display;
    if (global_display == NULL) {
        return -1;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->data == NULL) {
        return -1;
    }

    SPICE_DEBUG("isDown= %d, hardware_keycode=%d", isDown, hardware_keycode);

    if (!d->inputs)
    	return-1;

    scancode = hardware_keycode;
    if (isDown) {
        send_key(display, scancode, 1);
    } else {
	send_key(display, scancode, 0);
    }
    return 0;
}

int16_t SpiceGlibGlue_isConnected() {
    SPICE_DEBUG("isConnected int: %d bool: %d .", connections, (connections > 0));
    return (connections > 0);
}

int16_t SpiceGlibGlue_getNumberOfChannels() {
    if (mainconn == NULL) {
        return 0;
    } else {
        return mainconn->channels;
    }
}
