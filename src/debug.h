/*
 * debug.h
 * Copyright (C) Clearscene Ltd 2008 <wbooth@essentialcollections.co.uk>
 * 
 * debug.h is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * debug.h is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DEBUG
#define DEBUG

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
/*
 * a VERBOSITY setting of INFORMATION, will throw 'information, warning, error'
 */
extern int VERBOSITY;
extern char *LOG_DIR;
#else
/*
 * a VERBOSITY setting of INFORMATION, will throw 'information, warning, error'
 */
int VERBOSITY;
char *LOG_DIR;
#endif /* __cplusplus */

enum {
  SILENT = 0,
  ERROR,
  WARNING,
  INFORMATION,
  DEBUGM,
  SQLDEBUG
};

int trigger_log_verbosity( const int );
void oo_log(const char *, const int, const int, const char *, ...);
#define o_log( a, b, ... ) oo_log( __FILE__, __LINE__, a, b, ##__VA_ARGS__ )

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* DEBUG */
