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

/*
 * Follow Me printing Glue code.
 *
 * Functions to share/unshare printers with the guest.
 *
 * - The client program starts, and eventually will read the configuration file, where the
 * printers shared by this computer are stored.
 * - The spice library, will try to open a connection with the flexVDI agent, which happens in a different moment.
 * After the connection is opened the printers are shared.
 *
 * When the client gets the list of printers to be shared, it will call the glue to do so.
 * The glue:
 * - if the connection with the flexVDI agent is established, it will open the connection.
 * - if the connection is not opened, it will store the request in requestedSharedPrinters
 * - when the glue is notified by spice library that the connection with felxVDIAgent is opened
 * it will share the printers in the requestedList.
 *
 **/
#ifdef PRINTING
#include <stdio.h>
#include <string.h>

#include "glue-service.h"
#include "glib.h"
#include "flexvdi-port.h"

#define MAX_PRINTER_NAME_SIZE 1024

// TODO: if the client becomes multisession, this table must be part of the session.
// Printers shared with the guest.
GHashTable* sharedPrinters;

// Printers that the user wants to be shared.
// They may be shared, or not (ie: if there is no flexVDI agent)
// We will try to share them when the flexVDIAgent connects.
// This table is per executable; not per guest/session
GHashTable* requestedSharedPrinters;

// Temporary storage for the list of local printers
// printers: full list
// printer: not yet retrieved list.
GSList * localPrinters, * localPrinter;

/*
 * Get a list with the names of the printers installed in the system.
 * and store them locally in memory allocated/freed by C glue library.
 */
void SpiceGlibGlueGetLocalPrinterList() {

	SPICE_DEBUG("FMP: SpiceGlibGlueGetLocalPrinterList");
	// This call takes 2ms in my system with only local printers. Do not seem to be a need for an asynchronous call
	flexvdi_get_printer_list(&localPrinters);
	// Initialize iterator
	localPrinter = localPrinters;
 }

/*
 * Copies to printerName the name of the next printer in the list.
 * The StringBuilder passed in "printerName" must EnsureCapacity of at least MAX_PRINTER_NAME_SIZE,
 * which is the limit that we artificially impose.
 * In the next invocation after the last printer is retrieved,
 * the memory of the local list of printers is freed, and an empty ("") string passed back.
 * - isShared: true if printerName is shared with the guest.
 */
void SpiceGlibGlueGetNextLocalPrinter(char* printerName, int32_t* isShared) {

	SPICE_DEBUG("FMP: SpiceGlibGlueGetNextLocalPrinter()");
	localPrinter = g_slist_next(localPrinter);

	// When we get get past the end of the list, free the list
	if (localPrinter == NULL) {
		SPICE_DEBUG("FMP: No more printers.");
		printerName[0]= '\0';

		g_slist_free_full(localPrinters, g_free);
		localPrinters= NULL;
	} else {
		strncpy(printerName, (const char *)localPrinter->data, MAX_PRINTER_NAME_SIZE);
		gboolean inSharedSet = g_hash_table_contains (sharedPrinters, printerName);

		*isShared= inSharedSet?1:0;
		SPICE_DEBUG("FMP: Found printer %s shared %d", (const char *)localPrinter->data, *isShared);
	}
}

/*****************************************************************************/
/* Share/unshare printers */
/*****************************************************************************/

/*
 * Share the printer and note it down in sharedPrinters.
 */
static int32_t doSharePrinter(const char* printerName) {
	int32_t retVal = flexvdi_share_printer(printerName);
	if (retVal) {
		char* s = g_malloc(MAX_PRINTER_NAME_SIZE);
		strncpy(s, printerName, MAX_PRINTER_NAME_SIZE);
		g_hash_table_insert(sharedPrinters, s, NULL);
	}
	return retVal;
}

/* Share a printer. And add it to the list of printers we want to share.
 * If we are connected, share it now; otherwise share when flexVDI Agent connects with us.
 * returns 0 if failed (no agent running, ...)
 */
int32_t SpiceGlibGlueSharePrinter(const char* printerName) {
	SPICE_DEBUG("FMP: SpiceGlibGlueSharePrinter %s", printerName);

	// Add printer to requestedSharedPrinters
	char* s = g_malloc(MAX_PRINTER_NAME_SIZE);
	strncpy(s, printerName, MAX_PRINTER_NAME_SIZE);
	g_hash_table_insert(requestedSharedPrinters, s, NULL);

	return doSharePrinter(printerName);
}

/* Stop sharing a printer, and note down we don´t  want to share it.
 * returns 0 if failed (no agent running, ...)
 */
int32_t SpiceGlibGlueUnsharePrinter(const char* printerName) {
	SPICE_DEBUG("FMP: GlibGlueUnsharePrinter %s", printerName);

	g_hash_table_remove(requestedSharedPrinters, printerName);

	// !retVal means there is no connection, so there is no shared printer
	int32_t retVal= flexvdi_unshare_printer(printerName);
	if (retVal) {
		g_hash_table_remove(sharedPrinters, printerName);
	}
	return retVal;
}

/**
 * Returns >0 if the flexVDI agent is connected
 */
int32_t SpiceGlibGlueFlexVDIIsAgentConnected(void) {
	SPICE_DEBUG("FMP: SpiceGlibGlueFlexvdiIsAgentConnected()");
	int retVal = flexvdi_is_agent_connected();
	SPICE_DEBUG("FMP: returns %d", retVal);
	return retVal;
}

/**
 * Connect all printers that have been requested to be shared.
 * Callback called when flexVDI agent comes connects.
 */
static void share_all_requested_printers(gpointer data)
{
	SPICE_DEBUG("FMP: share_all_requested_printers()");

	// All printers have been disconnected, so we remove all elements from
	// sharedPrinters hashTable
	g_hash_table_remove_all(sharedPrinters);

	GHashTableIter iter;
	int size=g_hash_table_size(requestedSharedPrinters);
	SPICE_DEBUG("FMP: %d printers to be shared.", size);

	char *val;
	char *key;
	g_hash_table_iter_init (&iter, requestedSharedPrinters);
	while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val)) {
		doSharePrinter(key);
		SPICE_DEBUG("FMP: printer: %s", key);
	}
}

/* Creation of structures used by Follow Me Print Glue. */
void initializeFollowMePrinting() {
	SPICE_DEBUG("FMP: initializeFollowMePrinting()");
	requestedSharedPrinters = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	flexvdi_on_agent_connected(share_all_requested_printers, NULL);
}

/* Free structures used by Follow Me Print Glue. */
void disposeFollowMePrinting() {
	SPICE_DEBUG("FMP: disposeFollowMePrinting()");
	// Remove callback
	flexvdi_on_agent_connected(NULL, NULL);
	g_hash_table_destroy(requestedSharedPrinters);
}

/* Creation of structures that store the state of printer sharing within a session. */
void onConnectGuestFollowMePrinting() {
	SPICE_DEBUG("FMP: onConnectGuestFollowMePrinting()");
	sharedPrinters = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

/* Free per-connection structures. */
void onDisconnectGuestFollowMePrinting() {
	SPICE_DEBUG("FMP: onDisconnectGuestFollowMePrinting()");
	g_hash_table_destroy(sharedPrinters);
}
#endif /* PRINTING */
