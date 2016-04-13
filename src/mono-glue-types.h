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

#ifndef MONO_GLUE_TYPES_H_
#define MONO_GLUE_TYPES_H_

#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    int16_t buttonState;
} GlueMotionEvent;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t *rgba;
} MonoGlueCursor;

typedef struct {
    uint32_t x;
    uint32_t y;
} MonoGluePoint;

#endif /* MONO_GLUE_TYPES_H_ */
