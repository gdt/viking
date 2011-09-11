/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 * Copyright (C) 2006, Quy Tonthat <qtonthat@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**
 * SECTION:babel
 * @short_description: running external programs and redirecting to TRWLayers.
 *
 * GPSBabel may not be necessary for everything -- for instance,
 *   use a_babel_convert_from_shellcommand() with input_file_type == %NULL
 *   for an external program that outputs GPX.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "viking.h"
#include "gpx.h"
#include "babel.h"
#include <stdio.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <glib.h>
#include <glib/gstdio.h>

/* TODO in the future we could have support for other shells (change command strings), or not use a shell at all */
#define BASH_LOCATION "/bin/bash"

/**
 * Path to gpsbabel
 */
static gchar *gpsbabel_loc = NULL;

/**
 * Path to unbuffer
 */
static gchar *unbuffer_loc = NULL;

/**
 * a_babel_convert:
 * @vt:        The TRW layer to modify. All data will be deleted, and replaced by what gpsbabel outputs.
 * @babelargs: A string containing gpsbabel command line filter options. No file types or names should
 *             be specified.
 * @cb:        A callback function.
 *
 * This function modifies data in a trw layer using gpsbabel filters.  This routine is synchronous;
 * that is, it will block the calling program until the conversion is done. To avoid blocking, call
 * this routine from a worker thread.
 *
 * Returns: %TRUE on success
 */
gboolean a_babel_convert( VikTrwLayer *vt, const char *babelargs, BabelStatusFunc cb, gpointer user_data )
{
  int fd_src;
  FILE *f;
  gchar *name_src = NULL;
  gboolean ret = FALSE;
  gchar *bargs = g_strconcat(babelargs, " -i gpx", NULL);

  if ((fd_src = g_file_open_tmp("tmp-viking.XXXXXX", &name_src, NULL)) >= 0) {
    f = fdopen(fd_src, "w");
    a_gpx_write_file(vt, f);
    fclose(f);
    f = NULL;
    ret = a_babel_convert_from ( vt, bargs, cb, name_src, user_data );
    g_remove(name_src);
    g_free(name_src);
  }

  g_free(bargs);
  return ret;
}

#ifdef WINDOWS
static gboolean babel_general_convert( BabelStatusFunc cb, gchar **args, gpointer user_data )
{
  gboolean ret;
  FILE *f;
  gchar *cmd;
  gchar **args2;
  
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory( &si, sizeof(si) );
  ZeroMemory( &pi, sizeof(pi) );
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  
  cmd = g_strjoinv( " ", args);
  args2 = g_strsplit(cmd, "\\", 0);
  g_free(cmd);
  cmd = g_strjoinv( "\\\\", args2);
  g_free(args2);
  args2 = g_strsplit(cmd, "/", 0);
  g_free(cmd);
  cmd = g_strjoinv( "\\\\", args2);

  if( !CreateProcess(
             NULL,                   // No module name (use command line).
        (LPTSTR)cmd,           // Command line.
        NULL,                   // Process handle not inheritable.
        NULL,                   // Thread handle not inheritable.
        FALSE,                  // Set handle inheritance to FALSE.
        0,                      // No creation flags.
        NULL,                   // Use parent's environment block.
        NULL,                   // Use parent's starting directory.
        &si,                    // Pointer to STARTUPINFO structure.
        &pi )                   // Pointer to PROCESS_INFORMATION structure.
    ){
    g_error ( "CreateProcess failed" );
    ret = FALSE;
  }
  else {
    WaitForSingleObject(pi.hProcess, INFINITE);
    WaitForSingleObject(pi.hThread, INFINITE);
    
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    
    if ( cb )
      cb(BABEL_DONE, NULL, user_data);
    
    ret = TRUE;
  }

  g_strfreev( args2 );
  g_free( cmd );
 
  return ret;
}
/* Windows */
#else
/* Posix */
static gboolean babel_general_convert( BabelStatusFunc cb, gchar **args, gpointer user_data )
{
  gboolean ret = FALSE;
  GPid pid;
  GError *error = NULL;
  gint babel_stdout;

  if (!g_spawn_async_with_pipes (NULL, args, NULL, 0, NULL, NULL, &pid, NULL, &babel_stdout, NULL, &error)) {
    g_error("Async command failed: %s", error->message);
    g_error_free(error);
    ret = FALSE;
  } else {

    gchar line[512];
    FILE *diag;
    diag = fdopen(babel_stdout, "r");
    setvbuf(diag, NULL, _IONBF, 0);

    while (fgets(line, sizeof(line), diag)) {
      if ( cb )
        cb(BABEL_DIAG_OUTPUT, line, user_data);
    }
    if ( cb )
      cb(BABEL_DONE, NULL, user_data);
    fclose(diag);
    diag = NULL;
    waitpid(pid, NULL, 0);
    g_spawn_close_pid(pid);

    ret = TRUE;
  }
    
  return ret;
}
#endif /* Posix */

/**
 * babel_general_convert_from:
 * @cb: callback that is run upon new data from STDOUT (?)
 *     (TODO: STDERR would be nice since we usually redirect STDOUT)
 * @user_data: passed along to cb
 *
 * Runs args[0] with the arguments and uses the GPX module
 * to import the GPX data into layer vt. Assumes that upon
 * running the command, the data will appear in the (usually
 * temporary) file name_dst.
 *
 * Returns: %TRUE on success
 */
static gboolean babel_general_convert_from( VikTrwLayer *vt, BabelStatusFunc cb, gchar **args, const gchar *name_dst, gpointer user_data )
{
  gboolean ret = FALSE;
  FILE *f = NULL;
    
  /* No data required */
  if ( vt == NULL )
    return TRUE;

  if (babel_general_convert(cb, args, user_data)) {

    f = g_fopen(name_dst, "r");
    if (f) {
      a_gpx_read_file ( vt, f );
      fclose(f);
      f = NULL;
      ret = TRUE;
    }
  }
    
  return ret;
}

/**
 * a_babel_convert_from:
 * @vt:        The TRW layer to place data into. Duplicate items will be overwritten.
 * @babelargs: A string containing gpsbabel command line options. In addition to any filters, this string
 *             must include the input file type (-i) option.
 * @cb:	       Optional callback function. Same usage as in a_babel_convert().
 *
 * Loads data into a trw layer from a file, using gpsbabel.  This routine is synchronous;
 * that is, it will block the calling program until the conversion is done. To avoid blocking, call
 * this routine from a worker thread.
 *
 * Returns: %TRUE on success
 */
gboolean a_babel_convert_from( VikTrwLayer *vt, const char *babelargs, BabelStatusFunc cb, const char *from, gpointer user_data )
{
  int i,j;
  int fd_dst;
  gchar *name_dst = NULL;
  gboolean ret = FALSE;
  gchar *args[64];

  if ((fd_dst = g_file_open_tmp("tmp-viking.XXXXXX", &name_dst, NULL)) >= 0) {
    close(fd_dst);

    if (gpsbabel_loc ) {
      gchar **sub_args = g_strsplit(babelargs, " ", 0);

      i = 0;
      if (unbuffer_loc)
        args[i++] = unbuffer_loc;
      args[i++] = gpsbabel_loc;
      for (j = 0; sub_args[j]; j++) {
        /* some version of gpsbabel can not take extra blank arg */
        if (sub_args[j][0] != '\0')
          args[i++] = sub_args[j];
      }
      args[i++] = "-o";
      args[i++] = "gpx";
      args[i++] = "-f";
      args[i++] = (char *)from;
      args[i++] = "-F";
      args[i++] = name_dst;
      args[i] = NULL;

      ret = babel_general_convert_from ( vt, cb, args, name_dst, user_data );

      g_strfreev(sub_args);
    } else
      g_error("gpsbabel not found in PATH");
    g_remove(name_dst);
    g_free(name_dst);
  }

  return ret;
}

/**
 * a_babel_convert_from_shellcommand:
 *
 * Runs the input command in a shell (bash) and optionally uses GPSBabel to convert from input_file_type.
 * If input_file_type is %NULL, doesn't use GPSBabel. Input must be GPX (or Geocaching *.loc)
 *
 * Uses babel_general_convert_from() to actually run the command. This function
 * prepares the command and temporary file, and sets up the arguments for bash.
 */
gboolean a_babel_convert_from_shellcommand ( VikTrwLayer *vt, const char *input_cmd, const char *input_file_type, BabelStatusFunc cb, gpointer user_data )
{
  int fd_dst;
  gchar *name_dst = NULL;
  gboolean ret = FALSE;
  gchar **args;  

  if ((fd_dst = g_file_open_tmp("tmp-viking.XXXXXX", &name_dst, NULL)) >= 0) {
    gchar *shell_command;
    if ( input_file_type )
      shell_command = g_strdup_printf("%s | %s -i %s -f - -o gpx -F %s",
        input_cmd, gpsbabel_loc, input_file_type, name_dst);
    else
      shell_command = g_strdup_printf("%s > %s", input_cmd, name_dst);

    g_debug("%s: %s", __FUNCTION__, shell_command);
    close(fd_dst);

    args = g_malloc(sizeof(gchar *)*4);
    args[0] = BASH_LOCATION;
    args[1] = "-c";
    args[2] = shell_command;
    args[3] = NULL;

    ret = babel_general_convert_from ( vt, cb, args, name_dst, user_data );
    g_free ( args );
    g_free ( shell_command );
    g_remove(name_dst);
    g_free(name_dst);
  }

  return ret;
}

gboolean a_babel_convert_from_url ( VikTrwLayer *vt, const char *url, const char *input_type, BabelStatusFunc cb, gpointer user_data )
{
  static DownloadMapOptions options = { FALSE, FALSE, NULL, 0, a_check_kml_file};
  gint fd_src;
  int fetch_ret;
  gboolean ret = FALSE;
  gchar *name_src = NULL;
  gchar *babelargs = NULL;

  g_debug("%s: input_type=%s url=%s", __FUNCTION__, input_type, url);

  if ((fd_src = g_file_open_tmp("tmp-viking.XXXXXX", &name_src, NULL)) >= 0) {
    close(fd_src);
    g_remove(name_src);

    babelargs = g_strdup_printf(" -i %s", input_type);

    fetch_ret = a_http_download_get_url(url, "", name_src, &options, NULL);
    if (fetch_ret == 0)
      ret = a_babel_convert_from( vt, babelargs, NULL, name_src, NULL);
 
    g_remove(name_src);
    g_free(babelargs);
    g_free(name_src);
  }

  return ret;
}

static gboolean babel_general_convert_to( VikTrwLayer *vt, BabelStatusFunc cb, gchar **args, const gchar *name_src, gpointer user_data )
{
  if (!a_file_export(vt, name_src, FILE_TYPE_GPX, NULL)) {
    g_error("Error exporting to %s", name_src);
    return FALSE;
  }
       
  return babel_general_convert (cb, args, user_data);
}

gboolean a_babel_convert_to( VikTrwLayer *vt, const char *babelargs, BabelStatusFunc cb, const char *to, gpointer user_data )
{
  int i,j;
  int fd_src;
  gchar *name_src = NULL;
  gboolean ret = FALSE;
  gchar *args[64];  

  if ((fd_src = g_file_open_tmp("tmp-viking.XXXXXX", &name_src, NULL)) >= 0) {
    close(fd_src);

    if (gpsbabel_loc ) {
      gchar **sub_args = g_strsplit(babelargs, " ", 0);

      i = 0;
      if (unbuffer_loc)
        args[i++] = unbuffer_loc;
      args[i++] = gpsbabel_loc;
      args[i++] = "-i";
      args[i++] = "gpx";
      for (j = 0; sub_args[j]; j++)
        /* some version of gpsbabel can not take extra blank arg */
        if (sub_args[j][0] != '\0')
          args[i++] = sub_args[j];
      args[i++] = "-f";
      args[i++] = name_src;
      args[i++] = "-F";
      args[i++] = (char *)to;
      args[i] = NULL;

      ret = babel_general_convert_to ( vt, cb, args, name_src, user_data );

      g_strfreev(sub_args);
    } else
      g_error("gpsbabel not found in PATH");
    g_remove(name_src);
    g_free(name_src);
  }

  return ret;
}


void a_babel_init ()
{
  /* TODO allow to set gpsbabel path via command line */
  gpsbabel_loc = g_find_program_in_path( "gpsbabel" );
  if ( !gpsbabel_loc )
    g_error( "gpsbabel not found in PATH" );
  unbuffer_loc = g_find_program_in_path( "unbuffer" );
  if ( !unbuffer_loc )
    g_warning( "unbuffer not found in PATH" );

}

void a_babel_uninit ()
{
  g_free ( gpsbabel_loc );
  g_free ( unbuffer_loc );
}
