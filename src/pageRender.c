/*
 * pageRender.c
 * Copyright (C) Clearscene Ltd 2008 <wbooth@essentialcollections.co.uk>
 * 
 * handlers.c is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * handlers.c is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <time.h>

#include "main.h"
#include "db.h"
#include "dbaccess.h"
#include "utils.h"
#include "debug.h"
#include "validation.h" // for con_info struct - move me to web_handler.h
#include "localisation.h"
#ifdef CAN_SCAN
#include "scan.h"
#include "saneDispatcher.h"
#endif // CAN_SCAN //
#include "simpleLinkedList.h"

#include "pageRender.h"


/*
 *
 * Public Functions
 *
 */
#ifdef CAN_SCAN
char *getScannerList(void *lang) {

  char *answer = send_command( o_printf("internalGetScannerList:%s", (char *)lang) ); // scan.c
  o_log(DEBUGM, "RESPONSE WAS: %s", answer);

  return answer;
}

extern void *doScanningOperation(void *saneOpData) {

  struct doScanOpData *tr = saneOpData;
  char *command = o_printf("internalDoScanningOperation:%s,%s", tr->uuid, tr->lang);

  char *answer = send_command( command ); // scan.c
  o_log(DEBUGM, "RESPONSE WAS: %s", answer);

  if( 0 == strcmp(answer, "BUSY") ) {
    updateScanProgress(tr->uuid, SCAN_SANE_BUSY, 0);
  }
  free(answer);


  // Move:
  //   conver to JPEG 
  //   do OCR
  // to here (or at least the calls to do it are here).

  free(tr->uuid);
  free(tr->lang);
  free(tr);

#ifdef THREAD_JOIN
  pthread_exit(0);
#else
  return 0;
#endif
}

// Start the scanning process
//
char *doScan(char *deviceid, char *format, char *resolution, char *pages, char *ocr, char *pagelength, struct connection_info_struct *con_info) {

  char *ret = NULL;

  pthread_t thread;
  pthread_attr_t attr;
  int rc=0;
  uuid_t uu;
  char *scanUuid;
	
  o_log(DEBUGM,"Entering doScan");

  // Generate a uuid and scanning params object
  scanUuid = malloc(36+1); 
  uuid_generate(uu); // Note uuid_generate keeps open a file handle to /dev/[su]random, which we can't close - which mucks up valgrind output.
  uuid_unparse(uu, scanUuid);

  // Save requested parameters
  setScanParam(scanUuid, SCAN_PARAM_DEVNAME, deviceid);
  setScanParam(scanUuid, SCAN_PARAM_FORMAT, format);
  setScanParam(scanUuid, SCAN_PARAM_DO_OCR, ocr);
  setScanParam(scanUuid, SCAN_PARAM_REQUESTED_PAGES, pages);
  setScanParam(scanUuid, SCAN_PARAM_REQUESTED_RESOLUTION, resolution);
  setScanParam(scanUuid, SCAN_PARAM_LENGTH, pagelength);

  o_log(DEBUGM,"doScan setScanParam completed ");
  // save scan progress db record
  addScanProgress(scanUuid);

  // Create a new thread to start the scan process
  pthread_attr_init(&attr);
#ifdef THREAD_JOIN
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#else
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#endif /* THREAD_JOIN */

  struct doScanOpData *tr = malloc( sizeof(struct doScanOpData) );
  tr->uuid = o_strdup(scanUuid);
  tr->lang = o_strdup(con_info->lang);
  o_log(DEBUGM,"doScan launching doScanningOperation");
  rc = pthread_create(&thread, &attr, doScanningOperation, (void *)tr );
  if(rc != 0) {
    o_log(ERROR, "Failed to create a new thread - for scanning operation.");
    return NULL;
  }
#ifdef THREAD_JOIN
  con_info->thread = thread;
#endif /* THREAD_JOIN */

  // Build a response, to tell the client about the uuid (so they can query the progress)
  //
  ret = o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><DoScan><scanuuid>%s</scanuuid></DoScan></Response>", scanUuid);
  o_log(DEBUGM,"Leaving doScan");
  free(scanUuid);

  return ret;
}

char *nextPageReady(char *scanid, struct connection_info_struct *con_info) {

  pthread_t thread;
  pthread_attr_t attr;
  char *sql;
  int status=0;
  struct simpleLinkedList *rSet;

  sql = o_printf("SELECT status \
                  FROM scan_progress \
                  WHERE client_id = '%s'", scanid);

  rSet = runquery_db(sql);
  if( rSet != NULL ) {
    do {
      status = atoi(readData_db(rSet, "status"));
    } while ( nextRow( rSet ) );
    free_recordset( rSet );
  }
  free(sql);

  //
  if(status == SCAN_WAITING_ON_NEW_PAGE) {
    int rc;
    // Create a new thread to start the scan process
    pthread_attr_init(&attr);
#ifdef THREAD_JOIN
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#else
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#endif /* THREAD_JOIN */
    struct doScanOpData *tr = malloc( sizeof(struct doScanOpData) );
    tr->uuid = o_strdup(scanid);
    tr->lang = o_strdup(con_info->lang);
    rc = pthread_create(&thread, &attr, doScanningOperation, (void *)tr);
    if(rc != 0) {
      o_log(ERROR, "Failed to create a new thread - for scanning operation.");
      return NULL;
    }
#ifdef THREAD_JOIN
    con_info->thread = thread;
#endif
    updateScanProgress(scanid, SCAN_WAITING_ON_SCANNER, 0);
  } else {
    o_log(WARNING, "scan id indicates a status not waiting for a new page signal.");
    return NULL;
  }

  // Build a response, to tell the client about the uuid (so they can query the progress)
  //
  return o_strdup("<?xml version='1.0' encoding='utf-8'?>\n<Response><NextPageReady><result>OK</result></NextPageReady></Response>");
}


char *getScanningProgress(char *scanid) {

  struct simpleLinkedList *rSet;
  char *sql, *status=0, *value=0, *ret;

  sql = o_printf("SELECT status, value \
                  FROM scan_progress \
                  WHERE client_id = '%s'", scanid);

  rSet = runquery_db(sql);
  if( rSet != NULL ) {
    do {
      status = o_strdup(readData_db(rSet, "status"));
      value = o_strdup(readData_db(rSet, "value"));
    } while ( nextRow( rSet ) );
    free_recordset( rSet );
  }
  free(sql);

  // Build a response, to tell the client about the uuid (so they can query the progress)
  //
  ret = o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><ScanningProgress><status>%s</status><value>%s</value></ScanningProgress></Response>", status, value);
  free(status);
  free(value);

  return ret;
}
#endif // CAN_SCAN //

char *docFilter(char *subaction, char *textSearch, char *isActionRequired, char *startDate, char *endDate, char *tags, char *page, char *range, char *sortfield, char *sortorder, char *lang ) {

  struct simpleLinkedList *rSet;
  const char *type;
  char *docList, *actionrequired, *title, *docid, *humanReadableDate, 
    *rows, *token, *olds, *sql, *textWhere=NULL, *dateWhere=NULL, 
    *tagWhere=NULL, *actionWhere=NULL, *page_ret;
  int count = 0;

  if( 0 == strcmp(subaction, "fullList") ) {
    sql = o_strdup("SELECT DISTINCT docs.* FROM docs ");
  }
  else {
    sql = o_strdup("SELECT COUNT(docs.docid) c FROM docs ");
  }

  // Filter by title or OCR
  //
  if( textSearch!=NULL && strlen(textSearch) ) {
    textWhere = o_printf("(title LIKE '%%%s%%' OR ocrText LIKE '%%%s%%') ", textSearch, textSearch);
  }

  // Filter by ActionRequired
  //
  if( ( isActionRequired!=NULL ) && ( 0 == strcmp(isActionRequired, "true") ) ) {
    actionWhere = o_strdup("actionrequired=1 ");
  }

  // Filter by Doc Date
  //
  if( startDate && strlen(startDate) && endDate && strlen(endDate) ) {
    dateWhere = o_printf("date(docdatey || '-' || substr('00'||docdatem, -2) || '-' || substr('00'||docdated, -2)) BETWEEN date('%s') AND date('%s') ", startDate, endDate);
  }

  // Filter By Tags
  //
  if( tags && strlen(tags) ) {
    char delim, olddelim;
    tagWhere = o_strdup("tags.tagname IN (");
    count = 0;
    olds = tags;
    delim = ',';
    olddelim = delim;
    while(olddelim && *tags) {
      while(*tags && (delim != *tags)) tags++;
      *tags ^= olddelim = *tags; // olddelim = *s; *s = 0;
      token = o_strdup(olds);
      o_concatf(&tagWhere, "'%s',", token);
      free(token);
      count++;
      *tags++ ^= olddelim; // *s = olddelim; s++;
      olds = tags;
    }
    tagWhere[strlen(tagWhere)-1] = ')';

    o_concatf(&sql, " JOIN (SELECT docid, COUNT(docid) c FROM doc_tags \
                    JOIN tags ON tags.tagid = doc_tags.tagid \
                    WHERE %s GROUP BY docid HAVING c = %i ) t \
                    ON docs.docid = t.docid ", tagWhere, count);
    free(tagWhere);
  }


  // Build final SQL
  //
  if(textWhere || dateWhere || actionWhere ) {
    int prefixAnd = 0;
    conCat(&sql, "WHERE ");

    if(textWhere) {
      if(prefixAnd == 1) {
        conCat(&sql, "AND ");
      }
      else {
        prefixAnd = 1;
      }
      conCat(&sql, textWhere);
    }

    if(dateWhere) {
      if(prefixAnd == 1) {
        conCat(&sql, "AND ");
      }
      else {
        prefixAnd = 1;
      }
      conCat(&sql, dateWhere);
    }

    if(actionWhere) {
      if(prefixAnd == 1) {
        conCat(&sql, "AND ");
      }
      else {
        prefixAnd = 1;
      }
      conCat(&sql, actionWhere);
    }
  }
  free(textWhere);
  free(dateWhere);
  free(actionWhere);


  // Sort if required
  if( sortfield != NULL ) {
    // Which way
    char *direction;
    if( ( sortorder != NULL ) && (0 == strcmp(sortorder, "1") ) ) {
      direction = o_strdup("DESC");
    }
    else {
      direction = o_strdup("ASC");
    }

    switch(atoi(sortfield)) {
    case 0:
      o_concatf(&sql, "ORDER BY docid %s ", direction);
      break;

    case 1:
      o_concatf(&sql, "ORDER BY title %s ", direction);
      break;

    case 2:
      o_concatf(&sql, "ORDER BY filetype %s ", direction);
      break;

    case 3:
      o_concatf(&sql, "ORDER BY docdatey %s, docdatem %s, docdated %s ", direction, direction, direction);
      break;

    }
    free(direction);
  }

  // Paginate the results
  //
  if( (0 == strcmp(subaction, "fullList")) && (page != NULL) && (range != NULL) ) {
    int page_i = atoi( page );
    int range_i = atoi( range );
    int offset = (page_i - 1) * range_i;
    o_concatf(&sql, "LIMIT %d, %d ", offset, range_i);
    page_ret = o_printf("<page>%s</page>", page);
  }
  else {
    page_ret = o_strdup("");
  }

  // Get Results
  //
  rows = o_strdup("");
  count = 0;
  o_log(DEBUGM, "sql=%s", sql);

  rSet = runquery_db(sql);
  if( rSet != NULL ) {
    do {

      if( 0 == strcmp(subaction, "fullList") ) {
        count++;
        if( 0 == strcmp(readData_db(rSet, "filetype"), "1") ) {
          type = getString( "LOCAL_file_type_odf", lang );
        }
        else if( 0 == strcmp(readData_db(rSet, "filetype"), "3") ) {
          type = getString( "LOCAL_file_type_pdf", lang );
        }
        else if( 0 == strcmp(readData_db(rSet, "filetype"), "4") ) {
          type = getString( "LOCAL_file_type_image", lang );
        }
        else {
          type = getString( "LOCAL_file_type_scanned", lang );
        }
        actionrequired = o_strdup(readData_db(rSet, "actionrequired"));
        title = o_strdup(readData_db(rSet, "title"));
        docid = o_strdup(readData_db(rSet, "docid"));
        if( 0 == strcmp(title, "NULL") ) {
          free(title);
          title = o_strdup( getString( "LOCAL_default_title", lang ) ); 
        }
        const char *nodate = getString( "LOCAL_no_date_set", lang);
        humanReadableDate = dateHuman( o_strdup(readData_db(rSet, "docdatey")), 
                                       o_strdup(readData_db(rSet, "docdatem")), 
                                       o_strdup(readData_db(rSet, "docdated")),
                                       nodate );

        o_concatf(&rows, "<Row><docid>%s</docid><actionrequired>%s</actionrequired><title><![CDATA[%s]]></title><type>%s</type><date>%s</date></Row>", 
                           docid, actionrequired, title, type, humanReadableDate);
        free(docid);
        free(title);
        free(humanReadableDate);
        free(actionrequired);
      }
      else 
        count = atoi( readData_db(rSet, "c") );

    } while ( nextRow( rSet ) );
    free_recordset( rSet );
  }
  free(sql);

  docList = o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><DocFilter><count>%i</count>%s<Results>%s</Results></DocFilter></Response>", count, page_ret, rows);
  free(rows);
  free(page_ret);

  return docList;
}

char *titleAutoComplete(char *startsWith, char *notLinkedTo) {

  char *docid, *title, *data;
  char *result = o_strdup("{\"results\":[");
  char *line = o_strdup("{\"docid\":\"%s\",\"title\":\"%s\"}");

  char *sql = o_printf("SELECT DISTINCT docs.docid, docs.title FROM docs ");

  if(notLinkedTo != NULL) {
    o_concatf(&sql, "LEFT JOIN (SELECT * FROM doc_links \
                        WHERE linkeddocid = %s) dl \
                     ON docs.docid = dl.docid ", notLinkedTo);
  }

  o_concatf(&sql, "WHERE title LIKE '%s%%' ", startsWith);

  if(notLinkedTo != NULL) {
    o_concatf(&sql, "AND dl.docid IS NULL \
                     AND docs.docid != %s ", notLinkedTo);
  }

  conCat(&sql, "ORDER BY title ");

  struct simpleLinkedList *rSet = runquery_db(sql);
  if( rSet != NULL ) {
    int notFirst = 0;
    do {
      if(notFirst==1) 
        conCat(&result, ",");
      notFirst = 1;
      docid = o_strdup(readData_db(rSet, "docid"));
      title = o_strdup(readData_db(rSet, "title"));
      data = o_printf(line, docid, title);
      conCat(&result, data);
      free(data);
      free(docid);
      free(title);
    } while ( nextRow( rSet ) );
    free_recordset( rSet );
  }
  free(sql);
  free(line);
  conCat(&result, "]}");

  return result;
}

char *tagsAutoComplete(char *startsWith, char *docid) {

  struct simpleLinkedList *rSet;
  char *title, *data;
  char *result = o_strdup("{\"results\":[");
  char *line = o_strdup("{\"tag\":\"%s\"}");
  char *sql = o_printf(
    "SELECT tagname \
    FROM tags LEFT JOIN \
      (SELECT docid, tagid \
      FROM doc_tags \
      WHERE docid=%s) dt \
    ON tags.tagid = dt.tagid \
    WHERE dt.docid IS NULL \
    AND tagname like '%s%%' \
    ORDER BY tagname", docid, startsWith);

  rSet = runquery_db(sql);
  if( rSet != NULL ) {
    int notFirst = 0;
    do {
      if(notFirst==1) 
        conCat(&result, ",");
      notFirst = 1;
      title = o_strdup(readData_db(rSet, "tagname"));
      data = o_printf(line, title);
      conCat(&result, data);
      free(data);
      free(title);
    } while ( nextRow( rSet ) );
    free_recordset( rSet );
  }
  free(sql);
  free(line);
  conCat(&result, "]}");

  return result;
}

char *checkLogin( char *username, char *password, char *lang, struct simpleLinkedList *session_data ) {
  struct simpleLinkedList *rSet;
  int retry_throttle = 5;

  time_t current_time;
  time( &current_time );

  //
  // Check that a login attemp was made earlier than retry_throttle seconds ago.
  //
  struct simpleLinkedList *last_attempt = sll_searchKeys( session_data, "next_login_attempt" );
  if( last_attempt != NULL ) {
    struct tm last_attempt_date_struct;
    time_t last_attempt_time;

    char *last_attempt_date_string = (char *)last_attempt->data;
    strptime( last_attempt_date_string, "%a %b %d %T %Y", &last_attempt_date_struct);
    last_attempt_time = mktime( &last_attempt_date_struct );

    if( difftime( current_time, last_attempt_time ) <= retry_throttle ) {
      o_log( ERROR, "Login attempt was too soon (by %d seconds) after previous login fail", 
                                      retry_throttle - difftime( current_time, last_attempt_time ) );

      char *rtp = o_strdup( ctime( &current_time ) );
      chop( rtp );
      last_attempt->data = rtp;

      return o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><Login><result>FAIL</result><message>%s</message><retry_throttle>%d</retry_throttle></Login></Response>", getString("LOCAL_login_retry_too_soon", lang), retry_throttle);
    }

    else {
      // lsat attempt is not no longer used (unless it gets set again later), so we can remove it.
      free( last_attempt->data );
      free( last_attempt->key );
      sll_delete( last_attempt );
    }

  }


  //
  // Check we have a user of the name provided
  //
  char *sql = o_printf(
    "SELECT created \
    FROM user_access \
    WHERE username = '%s'", username );
  rSet = runquery_db( sql );
  free( sql );

  if( rSet == NULL ) {
    o_log( ERROR, "User provded an incorrect username!" );

    char *rtp = o_strdup( ctime( &current_time ) );
    chop( rtp );
    sll_insert( session_data, o_strdup("next_login_attempt"), rtp );

    return o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><Login><result>FAIL</result><message>%s</message><retry_throttle>%d</retry_throttle></Login></Response>", getString("LOCAL_bad_login", lang), retry_throttle);
  }


  // Create a password hash
  // Would have liked to do this directly in sqlite, but hashing functions
  // are not available, and defining one is a major dependency pain.
  char *salted_password = o_printf( "%s%s%s", readData_db(rSet, "created"), password, username );
  free_recordset( rSet );
  char *password_hash = str2md5( salted_password, strlen(salted_password) );
  free( salted_password );


  //
  // Update last_access if the password matches
  //
  sql = o_strdup( 
    "UPDATE user_access \
    SET last_access = datetime('now') \
    WHERE username = ? \
    AND password = ? ");
  struct simpleLinkedList *vars = sll_init();
  sll_append(vars, DB_TEXT );
  sll_append(vars, o_strdup(username) );
  sll_append(vars, DB_TEXT );
  sll_append(vars, o_strdup(password_hash) );
  runUpdate_db( sql, vars );
  free( sql );


  //
  // Get user info if the password matches
  //
  sql = o_printf(
    "SELECT realname, role \
    FROM user_access \
    WHERE username = '%s' \
    AND password = '%s'", username, password_hash );
  rSet = runquery_db( sql );
  free( sql );
  free( password_hash );

  if( rSet == NULL ) {
    o_log( ERROR, "User provded an incorrect password!" );

    char *rtp = o_strdup( ctime( &current_time ) );
    chop( rtp );
    sll_insert( session_data, o_strdup("next_login_attempt"), rtp );

    return o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><Login><result>FAIL</result><message>%s</message><retry_throttle>%d</retry_throttle></Login></Response>", getString("LOCAL_bad_login", lang), retry_throttle);
  }

  char *realname = o_strdup(readData_db(rSet, "realname"));
  char *role = o_strdup(readData_db(rSet, "role"));

  // Before we assign these values to a user session, ensure we've not 
  // gone nutz and fetched more than one record
  if ( nextRow( rSet ) ) {
    free_recordset( rSet );
    free( realname );
    free( role );
    o_log( ERROR, "User login check, returned more than one row!" );
    return NULL;
  }
  free_recordset( rSet );

  // Save the details to the session/
  sll_insert( session_data, o_strdup("username"), o_strdup(username) );
  sll_insert( session_data, o_strdup("realname"), realname );
  sll_insert( session_data, o_strdup("role"), role );

  return o_strdup("<?xml version='1.0' encoding='utf-8'?>\n<Response><Login><result>OK</result></Login></Response>");
}

char *doLogout( struct simpleLinkedList *session_data ) {
  
  struct simpleLinkedList *details;
  const char *elements[] = { "next_login_attempt", "username", "realname", "role", NULL };
  int i;

  for (i = 0; elements[i]; i++) {
    details = sll_searchKeys( session_data, elements[i] );
    if ( details != NULL ) {
      free( details->data );
      free( details->key );
      sll_delete( details );
    }
  }

  return o_strdup("<?xml version='1.0' encoding='utf-8'?>\n<Response><Logout><result>OK</result></Logout></Response>");
}

char *updateUser( char *username, char *realname, char *password, char *role, int can_edit_access, struct simpleLinkedList *session_data, char *lang) {
  struct simpleLinkedList *rSet;
  char *useUsername = NULL;
  char *created = NULL;
  char *sql;

  // Allow user not to specify the actual username
  if ( 0 == strcmp(username, "[current]") ) {
    rSet = sll_searchKeys( session_data, "username" );
    if ( rSet != NULL ) {
      useUsername = rSet->data;
    }
    else {
      o_log(ERROR, "A client tried to update her user, but was not logged in,");
      return o_strdup("<?xml version='1.0' encoding='utf-8'?>\n<Response><UpdateUser><result>FAIL</result></UpdateUser></Response>");
    }
  }
  else {
    if ( can_edit_access == 1 ) {
      useUsername = username;
    }
    else {
      o_log(ERROR, "Client specified a user to update, but they do not have permission.");
      return o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><error>%s</error></Response>", getString("LOCAL_no_access", lang) );
    }
  }

  // Check the calculated user is actually a user
  sql = o_printf(
    "SELECT created \
    FROM user_access \
    WHERE username = '%s'", useUsername );
  rSet = runquery_db( sql );
  free( sql );
  if( rSet == NULL ) {
    if ( can_edit_access == 1 ) {
      // Create a new user
      sql = o_strdup( "INSERT INTO user_access \
                      VALUES (?, '-no password yet-', '-not set yet-', \
                      datetime('now'), datetime('now'), 0);" );
      struct simpleLinkedList *vars = sll_init();
      sll_append(vars, DB_TEXT );
      sll_append(vars, o_strdup(useUsername) );
      runUpdate_db( sql, vars );
      free( sql );

      sql = o_printf(
        "SELECT created \
        FROM user_access \
        WHERE username = '%s'", useUsername );
      rSet = runquery_db( sql );
      free( sql );
    }
    else {
      return o_strdup("<?xml version='1.0' encoding='utf-8'?>\n<Response><UpdateUser><result>FAIL</result></UpdateUser></Response>");
    }
  }
  created = o_strdup( readData_db(rSet, "created") );
  free_recordset( rSet );

  // Update the users 'role'
  if ( role != NULL ) {
    if ( can_edit_access == 1 ) {
      char *sql = o_strdup(
        "UPDATE user_access \
        SET role = ? \
        WHERE username = ? " );
      struct simpleLinkedList *vars = sll_init();
      sll_append(vars, DB_TEXT );
      sll_append(vars, o_strdup(role) );
      sll_append(vars, DB_TEXT );
      sll_append(vars, o_strdup(useUsername) );
      runUpdate_db( sql, vars );
      free( sql );
    }
    else {
      o_log(ERROR, "Client specified a user to update, but they do not have permission.");
      return o_printf("<?xml version='1.0' encoding='utf-8'?>\n<Response><error>%s</error></Response>", getString("LOCAL_no_access", lang) );
    }
  }

  // Update the users 'realname'
  if ( realname != NULL ) {
    char *sql = o_strdup(
      "UPDATE user_access \
      SET realname = ? \
      WHERE username = ? " );
    struct simpleLinkedList *vars = sll_init();
    sll_append(vars, DB_TEXT );
    sll_append(vars, o_strdup(realname) );
    sll_append(vars, DB_TEXT );
    sll_append(vars, o_strdup(useUsername) );
    runUpdate_db( sql, vars );
    free( sql );
  }

  // Update the users 'password'
  if ( password != NULL ) {
    char *salted_password = o_printf( "%s%s%s", created, password, useUsername );
    char *password_hash = str2md5( salted_password, strlen(salted_password) );
    free( salted_password );
    char *sql = o_strdup(
      "UPDATE user_access \
      SET password = ? \
      WHERE username = ? " );
    struct simpleLinkedList *vars = sll_init();
    sll_append(vars, DB_TEXT );
    sll_append(vars, o_strdup(password_hash) );
    sll_append(vars, DB_TEXT );
    sll_append(vars, o_strdup(useUsername) );
    runUpdate_db( sql, vars );
    free( sql );
  }
  free( created );

  return o_strdup("<?xml version='1.0' encoding='utf-8'?>\n<Response><UpdateUser><result>OK</result></UpdateUser></Response>");
}

