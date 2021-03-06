/* Oren - a streaming media software development kit.
 *
 * Copyright © 2012 TinySoft, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __OREN_NC_UTILS_H__
#define __OREN_NC_UTILS_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

OrenNCSockaddr* oren_ncsockaddr_new_simple (const gchar *ip,
                                            guint16 port);

OrenNCSockaddr* oren_ncsockaddr_new_from_string (const gchar *str);

gchar* oren_ncsockaddr_to_string (OrenNCSockaddr *self);

OrenNCSocket* oren_ncsocket_new_from_addr (OrenNCSockaddr *addr);

OrenNCSocket* oren_ncsocket_new_datagram (const gchar *ip,
                                          guint16 port);

void oren_ncbuffer_write_data (OrenNCBuffer *self,
                               gconstpointer buffer,
                               gsize size);

void oren_ncbuffer_write_buffer (OrenNCBuffer *self,
                                 OrenNCBuffer *buffer,
                                 gsize offset,
                                 gsize length);

void oren_ncbuffer_write_string (OrenNCBuffer *self,
                                 const gchar *string);

void oren_ncbuffer_write_inetaddr (OrenNCBuffer *self,
                                   OrenNCInetAddress *address);

void oren_ncbuffer_write_sockaddr (OrenNCBuffer *self,
                                   OrenNCSockaddr *address);

const gchar* oren_ncbuffer_read_string (OrenNCBuffer *self);

OrenNCInetAddress* oren_ncbuffer_read_inetaddr (OrenNCBuffer *self);

OrenNCSockaddr* oren_ncbuffer_read_sockaddr (OrenNCBuffer *self);

void oren_ncbuffer_write_subbuf (OrenNCBuffer *self,
                                 OrenNCBuffer *subbuf);

OrenNCBuffer* oren_ncbuffer_read_subbuf (OrenNCBuffer *self);

gboolean oren_ncbuffer_load_file (OrenNCBuffer *self,
                                  const gchar *filename);

gboolean oren_ncbuffer_save_file (OrenNCBuffer *self,
                                  const gchar *filename);

void oren_ncbuffer_write_checksum (OrenNCBuffer *self,
                                   gsize offset);

gboolean oren_ncbuffer_check_checksum (OrenNCBuffer *self,
                                       gsize offset);

const gchar* oren_get_machine_code (void);

gchar* oren_escape_brackets (const gchar *str);

guint16 _oren_ncutils_calc_checksum (const guint16 *data,
                                     gsize count);

gint _oren_ncutils_int_compare (gconstpointer a,
                                gconstpointer b,
                                gpointer user_data);

gint _oren_ncutils_string_compare (gconstpointer a,
                                   gconstpointer b,
                                   gpointer user_data);

guint _oren_ncutils_int_hash (gconstpointer v);

gboolean _oren_ncutils_int_equal (gconstpointer v1,
                                  gconstpointer v2);

GPtrArray* _oren_clone_object_array (GPtrArray *array, GType type);

GPtrArray* _oren_create_inetaddr_array (gchar **strs);

gchar* _oren_inetaddr_array_to_string (GPtrArray *addrs);

GPtrArray* _oren_create_socket_array (GPtrArray *addrs, guint16 port);

gboolean _oren_register_socket_handlers (OrenNCReactor *reactor,
                                         GPtrArray *sockets,
                                         OrenNCHandler *handler);

OrenNCSocket* _oren_sockets_find_compatible (GPtrArray *sockets,
                                             OrenNCSocket *socket);

/* Utility log functions for scripts. */
#ifndef oren_debug
void oren_debug (const gchar *str);
#endif

#ifndef oren_message
void oren_message (const gchar *str);
#endif

#ifndef oren_warning
void oren_warning (const gchar *str);
#endif

#ifndef oren_critical
void oren_critical (const gchar *str);
#endif

#ifndef oren_error
void oren_error (const gchar *str);
#endif

G_END_DECLS

#endif /* __OREN_NC_UTILS_H__ */
