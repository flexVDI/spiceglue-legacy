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

#include "glue-spice-widget.h"

#define PTRFLAGS_DOWN 0x8000


#ifdef GLUE_SERVICE_C
SpiceDisplay*   global_display = NULL;
gboolean  global_disconnecting = FALSE;
GMainLoop            *mainloop = NULL;
int                connections = 0;
gboolean          soundEnabled = FALSE;
#else
extern SpiceDisplay*   global_display;
extern gboolean  global_disconnecting;
extern GMainLoop            *mainloop;
extern int                connections;
extern gboolean          soundEnabled;
#endif

