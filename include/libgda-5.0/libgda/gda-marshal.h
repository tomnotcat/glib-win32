
#ifndef ___gda_marshal_MARSHAL_H__
#define ___gda_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:VOID (/home/vivien/Devel/libgda/libgda/gda-marshal.list:25) */
#define _gda_marshal_VOID__VOID	g_cclosure_marshal_VOID__VOID

/* VOID:POINTER (/home/vivien/Devel/libgda/libgda/gda-marshal.list:26) */
#define _gda_marshal_VOID__POINTER	g_cclosure_marshal_VOID__POINTER

/* VOID:OBJECT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:27) */
#define _gda_marshal_VOID__OBJECT	g_cclosure_marshal_VOID__OBJECT

/* VOID:OBJECT,OBJECT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:28) */
extern void _gda_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* VOID:OBJECT,ENUM,OBJECT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:29) */
extern void _gda_marshal_VOID__OBJECT_ENUM_OBJECT (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* VOID:OBJECT,UINT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:30) */
extern void _gda_marshal_VOID__OBJECT_UINT (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* VOID:OBJECT,UINT,UINT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:31) */
extern void _gda_marshal_VOID__OBJECT_UINT_UINT (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);

/* VOID:OBJECT,BOOLEAN (/home/vivien/Devel/libgda/libgda/gda-marshal.list:32) */
extern void _gda_marshal_VOID__OBJECT_BOOLEAN (GClosure     *closure,
                                               GValue       *return_value,
                                               guint         n_param_values,
                                               const GValue *param_values,
                                               gpointer      invocation_hint,
                                               gpointer      marshal_data);

/* VOID:STRING,UINT,UINT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:33) */
extern void _gda_marshal_VOID__STRING_UINT_UINT (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);

/* VOID:ENUM,OBJECT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:34) */
extern void _gda_marshal_VOID__ENUM_OBJECT (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* VOID:UINT,POINTER (/home/vivien/Devel/libgda/libgda/gda-marshal.list:35) */
#define _gda_marshal_VOID__UINT_POINTER	g_cclosure_marshal_VOID__UINT_POINTER

/* VOID:INT,INT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:36) */
extern void _gda_marshal_VOID__INT_INT (GClosure     *closure,
                                        GValue       *return_value,
                                        guint         n_param_values,
                                        const GValue *param_values,
                                        gpointer      invocation_hint,
                                        gpointer      marshal_data);

/* VOID:INT,BOXED,BOXED (/home/vivien/Devel/libgda/libgda/gda-marshal.list:37) */
extern void _gda_marshal_VOID__INT_BOXED_BOXED (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* VOID:INT,BOOLEAN (/home/vivien/Devel/libgda/libgda/gda-marshal.list:38) */
extern void _gda_marshal_VOID__INT_BOOLEAN (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* VOID:STRING,INT (/home/vivien/Devel/libgda/libgda/gda-marshal.list:39) */
extern void _gda_marshal_VOID__STRING_INT (GClosure     *closure,
                                           GValue       *return_value,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint,
                                           gpointer      marshal_data);

/* POINTER:POINTER (/home/vivien/Devel/libgda/libgda/gda-marshal.list:40) */
extern void _gda_marshal_POINTER__POINTER (GClosure     *closure,
                                           GValue       *return_value,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint,
                                           gpointer      marshal_data);

/* BOOLEAN:POINTER (/home/vivien/Devel/libgda/libgda/gda-marshal.list:41) */
extern void _gda_marshal_BOOLEAN__POINTER (GClosure     *closure,
                                           GValue       *return_value,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint,
                                           gpointer      marshal_data);

G_END_DECLS

#endif /* ___gda_marshal_MARSHAL_H__ */

