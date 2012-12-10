


#ifndef __LIBGDA_SQL_PARSER_ENUM_TYPES_H__
#define __LIBGDA_SQL_PARSER_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS
GType gda_sql_parser_error_get_type (void);
#define GDA_TYPE_SQL_PARSER_ERROR (gda_sql_parser_error_get_type())
GType gda_sql_parser_mode_get_type (void);
#define GDA_TYPE_SQL_PARSER_MODE (gda_sql_parser_mode_get_type())
GType gda_sql_parser_flavour_get_type (void);
#define GDA_TYPE_SQL_PARSER_FLAVOUR (gda_sql_parser_flavour_get_type())
GType gda_sql_error_get_type (void);
#define GDA_TYPE_SQL_ERROR (gda_sql_error_get_type())
GType gda_sql_statement_type_get_type (void);
#define GDA_TYPE_SQL_STATEMENT_TYPE (gda_sql_statement_type_get_type())
GType gda_sql_any_part_type_get_type (void);
#define GDA_TYPE_SQL_ANY_PART_TYPE (gda_sql_any_part_type_get_type())
GType gda_sql_statement_compound_type_get_type (void);
#define GDA_TYPE_SQL_STATEMENT_COMPOUND_TYPE (gda_sql_statement_compound_type_get_type())
GType gda_sql_operator_type_get_type (void);
#define GDA_TYPE_SQL_OPERATOR_TYPE (gda_sql_operator_type_get_type())
GType gda_sql_select_join_type_get_type (void);
#define GDA_TYPE_SQL_SELECT_JOIN_TYPE (gda_sql_select_join_type_get_type())
G_END_DECLS

#endif /* __LIBGDA_ENUM_TYPES_H__ */



