#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include "utils.h"

// "Max length to which the queue of pending connections for sockfd may grow".
// listen(2)
#define MAX_QUEUE 10

/* Flags */
extern int aflag, pflag, dflag;

/* Handle the connection with the client, retrieving informations such as OP and
 * pathnames*/
void *handle_connection(void *arg);

/* Handle a read request by the client. This consists in:
   - Locking the file so that nobody can write on it.
   - Sending the file to the client
   - Release the lock by closing the file descriptor.
 */
void handle_read(int client_sd, char *read_path);

/* Handle a write request by the client. This consists in:
   - Creating the file specified by client if it doesn't exist.
   - Locking the file so that nobody else can read/write it.
   - Handling the file transmission from the client.
   - Release the lock by closing the file descriptor.
 */
void handle_write(int client_sd, char *write_path);

/* Perform an ls -la lookalike on the path specified from the client and
   send back the output.
 */
void handle_ls(int client_sd, char *path);

/* Write a notification status to the user. The statuses are defined in
 * "utils.h"
 */
void notify_status(int client_sd, int status);

/* Check if the root path exists, otherwise create it.
   Returns:
   0 on success
   -1 on failure
*/
int get_or_create_ft_root_directory(char *path, DIR **dir);

#endif
