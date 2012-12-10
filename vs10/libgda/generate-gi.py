#!/usr/bin/env python
import sys, os

name = "g-ir-scanner.py"
path = os.environ['Path']
scanner = ""

for it in path.split (';'):
    if os.path.exists (it + os.path.sep + name):
        scanner = it + os.path.sep + name
        break

srcs = '''../SQLHeavy-vs10.h \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-backup.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-common-function.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-cursor.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-database.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-enums.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-error.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-mutable-record.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-profiling-database.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-query.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-queryable.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-query-result.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-record.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-record-set.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-row.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-table.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-table-cursor.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-transaction.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-user-functions.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-value.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-version.c \
../sqlheavy-0.1.1/sqlheavy/sqlheavy-versioned-database.c'''

os.system ("cd " + sys.argv[1] + " && python.exe \"" + scanner + "\" --add-include-path=. --warn-all --namespace=SQLite --identifier-prefix=SQLHeavy --symbol-prefix=sql_heavy \
 --nsversion=1.0 --no-libtool --include=GObject-2.0 --library=" + sys.argv[2] + " -Isqlheavy-0.1.1/sqlheavy -I../sqlite-amalgamation-3071401 --output SQLite-1.0.gir " + srcs +
" && g-ir-compiler --includedir=. SQLite-1.0.gir -o SQLite-1.0.typelib")
