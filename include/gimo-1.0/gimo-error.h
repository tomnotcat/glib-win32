/* GIMO - A plugin framework based on GObject.
 *
 * Copyright (C) 2012 TinySoft, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __GIMO_ERROR_H__
#define __GIMO_ERROR_H__

#include <glib.h>

G_BEGIN_DECLS

#define GIMO_LIST_TO_ENUM(ITEM) ITEM,
#define GIMO_LIST_ITEM(ITEM) ITEM
#define GIMO_LIST_TO_STRING(ITEM) case ITEM: return _(#ITEM);

#define GIMO_ERROR_LIST(_, __)        \
    _(GIMO_ERROR_NONE)                \
    _(GIMO_ERROR_NO_MEMORY)           \
    _(GIMO_ERROR_NO_FILE)             \
    _(GIMO_ERROR_OPEN_FILE)           \
    _(GIMO_ERROR_INVALID_FILE)        \
    _(GIMO_ERROR_INVALID_ID)          \
    _(GIMO_ERROR_NO_OBJECT)           \
    _(GIMO_ERROR_INVALID_OBJECT)      \
    _(GIMO_ERROR_INVALID_STATE)       \
    _(GIMO_ERROR_INVALID_RETURN)      \
    _(GIMO_ERROR_NO_PLUGIN)           \
    _(GIMO_ERROR_NO_EXTPOINT)         \
    _(GIMO_ERROR_NO_EXTENSION)        \
    _(GIMO_ERROR_CONFLICT)            \
    _(GIMO_ERROR_NO_TYPE)             \
    _(GIMO_ERROR_INVALID_TYPE)        \
    _(GIMO_ERROR_LOAD)                \
    _(GIMO_ERROR_UNLOAD)              \
    _(GIMO_ERROR_NO_SYMBOL)           \
    _(GIMO_ERROR_INVALID_SYMBOL)      \
    _(GIMO_ERROR_NO_ATTRIBUTE)        \
    __(GIMO_ERROR_INVALID_ATTRIBUTE)

/**
 * GimoErrors:
 * @GIMO_ERROR_NONE: no error
 * @GIMO_ERROR_NO_MEMORY: out of memory
 * @GIMO_ERROR_NO_FILE: file not exist
 * @GIMO_ERROR_OPEN_FILE: open file error
 * @GIMO_ERROR_INVALID_FILE: invalid file type
 * @GIMO_ERROR_INVALID_ID: invalid identifier
 * @GIMO_ERROR_NO_OBJECT: object not found
 * @GIMO_ERROR_INVALID_OBJECT: invalid object
 * @GIMO_ERROR_INVALID_STATE: invalid state
 * @GIMO_ERROR_INVALID_RETURN: invalid return
 * @GIMO_ERROR_NO_EXTPOINT: extension point not exist
 * @GIMO_ERROR_NO_EXTENSION: extension not exist
 * @GIMO_ERROR_CONFLICT: object conflict
 * @GIMO_ERROR_NO_TYPE: type not found
 * @GIMO_ERROR_INVALID_TYPE: invalid type
 * @GIMO_ERROR_LOAD: load module error
 * @GIMO_ERROR_UNLOAD: unload module error
 * @GIMO_ERROR_NO_SYMBOL: symbol not found
 * @GIMO_ERROR_INVALID_SYMBOL: invalid symbol
 * @GIMO_ERROR_NO_ATTRIBUTE: attribute not found
 * @GIMO_ERROR_INVALID_ATTRIBUTE: invalid attribute
 */
typedef enum {
    GIMO_ERROR_LIST (GIMO_LIST_TO_ENUM,
                     GIMO_LIST_ITEM)
} GimoErrors;

void gimo_trace_error (gboolean trace);

void gimo_set_error (gint code);

void gimo_set_error_string (gint code, const gchar *string);

void gimo_set_error_full (gint code, const gchar *format, ...);

gint gimo_get_error (void);

gchar* gimo_dup_error_string (void);

void gimo_clear_error (void);

const gchar* gimo_error_to_string (gint code);

#define gimo_set_error_return(_code) \
    G_STMT_START { \
        gimo_set_error (_code); \
        return; \
    } G_STMT_END

#define gimo_set_error_return_val(_code, _val) \
    G_STMT_START { \
        gimo_set_error (_code); \
        return _val; \
    } G_STMT_END

G_END_DECLS

#endif /* __GIMO_ERROR_H__ */
