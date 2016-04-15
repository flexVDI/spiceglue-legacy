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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>
#include <glib-object.h>

#include <sys/stat.h>
#define SPICY_C
#include <spice-gtk/glib-compat.h>
#include "glue-spice-widget.h"
#include <spice-gtk/spice-audio.h>
#include <spice-gtk/spice-common.h>
#include "glue-spicy.h"
#include "glue-service.h"


G_DEFINE_TYPE (SpiceWindow, spice_window, G_TYPE_OBJECT);

static void connection_destroy(spice_connection *conn);

/* ------------------------------------------------------------------ */

static SpiceWindow *create_spice_window(spice_connection *conn, SpiceChannel *channel, int id)
{
    SpiceWindow *win;

    win = g_new0 (SpiceWindow, 1);
    win->id = id;
    //win->monitor_id = monitor_id;
    win->conn = conn;
    win->display_channel = channel;

    win->spice = (spice_display_new(conn->session, id));
    return win;
}

static void destroy_spice_window(SpiceWindow *win)
{
    if (win == NULL)
        return;

    SPICE_DEBUG("destroy window (#%d:%d)", win->id, win->monitor_id);
    //g_object_unref(win->ag);
    //g_object_unref(win->ui);
    //gtk_widget_destroy(win->toplevel);
    g_object_unref(win);
}

static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                               gpointer data)
{
    spice_connection *conn = data;
    char password[64];
    int rc = -1;

    switch (event) {
    case SPICE_CHANNEL_OPENED:
    	g_message("main channel: opened");

    	//recent_add(conn->session);
        break;
    case SPICE_CHANNEL_SWITCHING:
        g_message("main channel: switching host");
        break;
    case SPICE_CHANNEL_CLOSED:
        /* this event is only sent if the channel was succesfully opened before */
        g_message("main channel: closed");
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        g_message("main channel: failed to connect");
        //rc = connect_dialog(conn->session);
        if (rc == 0) {
            connection_connect(conn);
        } else {
            connection_disconnect(conn);
        }
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        g_warning("main channel: auth failure (wrong password?)");
        strcpy(password, "");
        /* FIXME i18 */
        //rc = ask_user(NULL, _("Authentication"),
        //              _("Please enter the spice server password"),
        //              password, sizeof(password), true);
        if (rc == 0) {
            g_object_set(conn->session, "password", password, NULL);
            connection_connect(conn);
        } else {
            connection_disconnect(conn);
        }
        break;
    default:
        /* TODO: more sophisticated error handling */
        g_warning("unknown main channel event: %d", event);
        /* connection_disconnect(conn); */
        break;
    }
}

/*
 * TODO: currently connected only to inputs, playback and display channels
 * Probably we should connect this to more channels.
 */
static void generic_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
				  gpointer data)
{
    // TODO: Improve this long chain of if. There must be a function for doing this
    char* channel_name= "unknown";
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
	channel_name="main";
    } else if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
	channel_name="display";
    } else if (SPICE_IS_INPUTS_CHANNEL(channel)) {
	channel_name="inputs";
    } else if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
    	channel_name="audio";
    } else if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
    	channel_name="usbredir";
    } else if (SPICE_IS_PORT_CHANNEL(channel)) {
    	channel_name="port";
    } else if (SPICE_IS_RECORD_CHANNEL (channel)) {
	channel_name="record";
    }

    int rc = -1;
    spice_connection *conn = data;

    switch (event) {

    case SPICE_CHANNEL_CLOSED:
        g_warning("%s channel closed", channel_name);
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        g_message("%s channel: failed to connect", channel_name);
        //rc = connect_dialog(conn->session);
        if (rc == 0) {
            connection_connect(conn);
        } else {
            connection_disconnect(conn);
        }
        break;
    default:
        /* TODO: more sophisticated error handling */
        g_warning("%s channel event: %d", channel_name, event);
        /* connection_disconnect(conn); */
        break;
    }
}

static void main_mouse_update(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    gint mode;

    g_object_get(channel, "mouse-mode", &mode, NULL);
    switch (mode) {
    case SPICE_MOUSE_MODE_SERVER:
        conn->mouse_state = "server";
        break;
    case SPICE_MOUSE_MODE_CLIENT:
        conn->mouse_state = "client";
        break;
    default:
        conn->mouse_state = "?";
        break;
    }
}

static void main_agent_update(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;

    g_object_get(channel, "agent-connected", &conn->agent_connected, NULL);
    conn->agent_state = conn->agent_connected ? _("yes") : _("no");
    SPICE_DEBUG(" PENDIENTE actualizar etiq. estado de ventana cliente. agent-state %s", conn->agent_state);
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    conn->channels++;
    SPICE_DEBUG("new channel (#%d)", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("new main channel");
        conn->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(main_channel_event), conn);
        g_signal_connect(channel, "main-mouse-update",
                         G_CALLBACK(main_mouse_update), conn);
        g_signal_connect(channel, "main-agent-update",
                         G_CALLBACK(main_agent_update), conn);
        main_mouse_update(channel, conn);
        main_agent_update(channel, conn);
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id >= SPICE_N_ELEMENTS(conn->wins))
            return;
        if (conn->wins[id] != NULL)
            return;
        SPICE_DEBUG("new display channel (#%d)", id);
        conn->wins[id] = create_spice_window(conn, channel, id);
        SPICE_DEBUG("display-mark not connected Aqui el original conecta display_monitors");
        //g_signal_connect(channel, "display-mark",
        //                 G_CALLBACK(display_mark), conn->wins[id]); Mostrar / ocultar imagen cuando lo pide el spice server
        //update_auto_usbredir_sensitive(conn);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(generic_channel_event), conn);
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        SPICE_DEBUG("new inputs channel");
        //g_signal_connect(channel, "inputs-modifiers",
        //                 G_CALLBACK(inputs_modifiers), conn);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(generic_channel_event), conn);
    }

    if (soundEnabled && SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("new audio channel");
        conn->audio = spice_audio_get(s, NULL);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(generic_channel_event), conn);
    }

    //if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
    //    update_auto_usbredir_sensitive(conn);
    //}

    if (SPICE_IS_PORT_CHANNEL(channel)) {
	//    g_signal_connect(channel, "notify::port-opened",
	//                     G_CALLBACK(port_opened), conn);
	//    g_signal_connect(channel, "port-data",
	//                     G_CALLBACK(port_data), conn);
        spice_channel_connect(channel);
    }
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SPICE_DEBUG( "glue-spicy: channel_destroy called");

    spice_connection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("zap main channel");
        conn->main = NULL;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id >= SPICE_N_ELEMENTS(conn->wins))
            return;
        if (conn->wins[id] == NULL)
            return;
        SPICE_DEBUG("zap display channel (#%d)", id);
        destroy_spice_window(conn->wins[id]);
        conn->wins[id] = NULL;
    }

    if (soundEnabled && SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("zap audio channel");
    }

    //if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
    //    update_auto_usbredir_sensitive(conn);
    //}

    //if (SPICE_IS_PORT_CHANNEL(channel)) {
    //    if (SPICE_PORT_CHANNEL(channel) == stdin_port)
    //        stdin_port = NULL;
    //}

    conn->channels--;
    if (conn->channels > 0) {
    	char buf[100];
        snprintf (buf, 100, "Number of channels: %d", conn->channels);
    	SPICE_DEBUG("glue-spice: %s", buf);
        return;
    }

    connection_destroy(conn);
}

static void migration_state(GObject *session,
                            GParamSpec *pspec, gpointer data)
{
    SpiceSessionMigration mig;

    g_object_get(session, "migration-state", &mig, NULL);
    if (mig == SPICE_SESSION_MIGRATION_SWITCHING)
        g_message("migrating session");
}

spice_connection *connection_new(void)
{
    spice_connection *conn;
    //SpiceUsbDeviceManager *manager;

    conn = g_new0(spice_connection, 1);
    conn->session = spice_session_new();
    g_signal_connect(conn->session, "channel-new",
                     G_CALLBACK(channel_new), conn);
    g_signal_connect(conn->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), conn);
    g_signal_connect(conn->session, "notify::migration-state",
                     G_CALLBACK(migration_state), conn);

    //manager = spice_usb_device_manager_get(conn->session, NULL);
    //if (manager) {
    //    g_signal_connect(manager, "auto-connect-failed",
    //                     G_CALLBACK(usb_connect_failed), NULL);
    //    g_signal_connect(manager, "device-error",
    //                     G_CALLBACK(usb_connect_failed), NULL);
    //}

    connections++;
    SPICE_DEBUG("%s (%d)", __FUNCTION__, connections);
    return conn;
}

void connection_connect(spice_connection *conn)
{
    conn->disconnecting = false;
    spice_session_connect(conn->session);
}

void connection_disconnect(spice_connection *conn)
{
    if (conn->disconnecting || global_disconnecting)
        return;
    conn->disconnecting = true;
    spice_session_disconnect(conn->session);
}

extern spice_connection *mainconn;

static void connection_destroy(spice_connection *conn)
{
    SPICE_DEBUG("glue-spicy: connection_destroy()");
    mainconn = NULL;
    g_object_unref(conn->session);
    free(conn);

    connections--;
    SPICE_DEBUG("%s (%d)", __FUNCTION__, connections);
    if (connections > 0) {
        return;
    }

    //g_main_loop_quit(mainloop);
}

/* Saver config parameters to session Object*/
void spice_session_setup(SpiceSession *session, const char *host,
			 const char *port,
			 const char *tls_port,
			 const char *ws_port,
			 const char *password,
			 const char *ca_file,
			 const char *cert_subj) {

    SPICE_DEBUG("spice_session_setup host=%s, ws_port=%s, port=%s, tls_port=%s", host, ws_port, port, tls_port);
    g_return_if_fail(SPICE_IS_SESSION(session));
    
    if (host)
        g_object_set(session, "host", host, NULL);
    // If we receive "-1" for a port, we assume the port is not set.
    if (port && strcmp (port, "-1") != 0)
        g_object_set(session, "port", port, NULL);
    if (tls_port && strcmp (tls_port, "-1") != 0)
        g_object_set(session, "tls-port", tls_port, NULL);
    if (ws_port && strcmp (ws_port, "-1") != 0)
        g_object_set(session, "ws-port", ws_port, NULL);
    if (password)
        g_object_set(session, "password", password, NULL);
    if (ca_file)
        g_object_set(session, "ca-file", ca_file, NULL);
    if (cert_subj)
        g_object_set(session, "cert-subject", cert_subj, NULL);
}
