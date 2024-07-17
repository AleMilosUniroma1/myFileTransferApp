#ifndef CLIENT_H
#define CLIENT_H

#include <sys/socket.h>

#include "utils.h"

/* Flags */
extern int aflag, pflag, dflag;
extern int readflag, writeflag, listflag;
extern int fflag, oflag;

/* Communication with the server */

/* Read locally the content of localpath and write it to the server at
 * remotepath */
void write_to_server(int client_sd, char *localpath, char *remotepath);

/* Read the content of remotepath from the server and write it locally on
 * localpath */
void read_from_server(int client_sd, char *remotepath, char *localpath);

/* Get an ls -la lookalike output at remotepath from the server and output it to
 * stdout */
void ls_from_server(int client_sd, char *remotepath);

/* Handle the recv() by writing to stdout in case of success status flags sent
 * by the server. Othwerise write to stderr and call exit(1);
   The status flags are define in "utils.h".
 */
void receive_status(int client_sd, char *message);

#endif