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

#ifndef _ANDROID_SPICY_H
#define _ANDROID_SPICY_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib/gi18n.h>
#include "gobject/gtype.h"

#include <sys/stat.h>
#include <spice-session.h>
#include <spice-gtk/spice-audio.h>
#include <spice-gtk/spice-common.h>

#include "glue-spice-widget.h"

typedef struct spice_connection spice_connection;

enum {
    STATE_SCROLL_LOCK,
    STATE_CAPS_LOCK,
    STATE_NUM_LOCK,
    STATE_MAX,
};

typedef struct _SpiceWindow SpiceWindow;
typedef struct _SpiceWindowClass SpiceWindowClass;

struct _SpiceWindow {
    GObject          object;
    spice_connection *conn;
    gint             id;
    gint             monitor_id;
    SpiceDisplay      *spice;
    bool             fullscreen;
    bool             mouse_grabbed;
    SpiceChannel     *display_channel;
#ifdef WIN32
    gint             win_x;
    gint             win_y;
#endif
    bool             enable_accels_save;
    bool             enable_mnemonics_save;
};

struct _SpiceWindowClass
{
    GObjectClass parent_class;
};

static void spice_window_class_init (SpiceWindowClass *klass) {}
static void spice_window_init (SpiceWindow *self) {}

#define CHANNELID_MAX 4
#define MONITORID_MAX 4

// FIXME: turn this into an object, get rid of fixed wins array, use
// signals to replace the various callback that iterate over wins array
struct spice_connection {
    SpiceSession     *session;
    SpiceMainChannel *main;
    SpiceWindow     *wins[CHANNELID_MAX * MONITORID_MAX]; // Ventanas asociadas a la conexi�n. Una por monitor
    SpiceAudio       *audio;
    const char       *mouse_state;
    const char       *agent_state;
    gboolean         agent_connected;
    int              channels;
    int              disconnecting;
};

void spice_session_setup(SpiceSession *session, const char *host,
			 const char *port,
			 const char *tls_port,
			 const char *ws_port,
			 const char *password,
			 const char *ca_file,
			 const char *cert_subj);

spice_connection *connection_new(void);
void connection_connect(spice_connection *conn);
void connection_disconnect(spice_connection *conn);

#endif /* _ANDROID_SPICY_H */
