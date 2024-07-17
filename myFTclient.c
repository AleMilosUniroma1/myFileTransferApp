#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <unistd.h>

#include "myFTclient.h"

/* Flags */
int aflag, pflag, dflag;
int readflag, writeflag, listflag;
int fflag, oflag;

int main(int argc, char **argv) {

  int opt;
  int server_port;
  char *server_address = NULL;

  char *f_arg_path = NULL;
  char *o_arg_path = NULL;

  while ((opt = getopt(argc, argv, "a:p:drwlhf:o:")) != -1) {
    char *eptr;
    switch (opt) {
    case 'a':
      server_address = xstrdup(optarg);
      aflag = 1;
      break;

    case 'p':
      // Check positivity and make sure sscanf doesn't fail
      if (isdigit(optarg[0]) != 0 && sscanf(optarg, "%u", &server_port) != 0) {
        pflag = 1;
      }
      break;

    case 'r':
      readflag = 1;
      break;

    case 'w':
      writeflag = 1;
      break;

    case 'l':
      listflag = 1;
      break;

    case 'f':
      fflag = 1;
      f_arg_path = xstrdup(optarg);
      break;

    case 'o':
      oflag = 1;
      o_arg_path = xstrdup(optarg);
      break;

    case 'h':
    default:
      printf("usage: %s [-w | -r | -l] [-a address] [-p port] [-f path] [-o "
             "path]\n\nWhen performing a -w operation\n  -f specifies the "
             "local_path/filename_local (to be read)\n  -o specifies the "
             "remote_path/filename_remote (to write in)\n  NOTE: -o can be "
             "omitted, and -f "
             "will be "
             "used as the remote filepath.\n\nWhen performing a -r operation\n "
             " -f specifies the "
             "remote_path/filename_remote (to be read)\n  -o specifies the "
             "local_path/filename_local (to write in)\n  NOTE: -o can be "
             "omitted, and -f "
             "will be "
             "used as the local filepath.\n\nWhen performing a -l operation\n  "
             "-f specifies the "
             "remote_path (to be listed)\n",

             argv[0]);
      exit(EXIT_SUCCESS);
      break;
    }
  }

  // The operations are exclusive, only one can be performed at a time.
  int flag_op_count = writeflag + readflag + listflag;

  if (!aflag || !pflag || flag_op_count != 1) {
    printf("usage: %s [-w | -r | -l] [-a address] [-p port]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  if (writeflag && !fflag) {
    printf("[-f local_path] must be specified when performing a "
           "-w operation\n");
    exit(EXIT_FAILURE);
  }
  if (readflag && !fflag) {
    printf("[-f remote_path] must be specified when performing a "
           "-r operation\n");
    exit(EXIT_FAILURE);
  }
  if (listflag && !fflag) {
    printf("[-f remote_path] must be specified when performing a "
           "-l operation\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in server;
  server.sin_addr.s_addr = inet_addr(server_address);
  server.sin_family = AF_INET;
  server.sin_port = htons(server_port);

  int client_sd;
  if ((client_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  };

  if (connect(client_sd, (struct sockaddr *)&server,
              (socklen_t)sizeof(server)) < 0) {
    perror("connect");
    exit(EXIT_FAILURE);
  }

  printf("Client  connected\n");
  /* Formatting propertly the request */

  if (writeflag) {
    write_to_server(client_sd, f_arg_path, o_arg_path);
  }

  if (readflag) {
    read_from_server(client_sd, f_arg_path, o_arg_path);
  }

  if (listflag) {
    ls_from_server(client_sd, f_arg_path);
  }

  //////////////
  // Free Memory
  free(server_address);
  free(f_arg_path);
  free(o_arg_path);
  //////////////
}

void write_to_server(int client_sd, char *localpath, char *remotepath) {
  if (localpath == NULL) {
    fprintf(stderr, "Missing File\n");
    exit(EXIT_FAILURE);
  }

  if (access(localpath, R_OK) != 0) {
    perror(localpath);
    exit(EXIT_FAILURE);
  }

  char op = WRITE;
  xwrite_all(client_sd, &op, sizeof(char));

  receive_status(client_sd, "op");

  if (!oflag) {
    // If -o not specified, -f filepath will be used to read file on server
    remotepath = xstrdup(localpath);
  }

  // Send filepath/filename to server
  xwrite_all(client_sd, remotepath, BUFSIZE);

  // Check file exists on server
  receive_status(client_sd, "input file");

  // Send file size
  struct stat obj;
  stat(localpath, &obj);

  int fd = open(localpath, O_RDONLY);
  int f_size = obj.st_size;

  // Tell the server the file size
  xwrite_all(client_sd, &f_size, sizeof(int));

  // send file data
  sendfile(client_sd, fd, NULL, f_size);
}

void read_from_server(int client_sd, char *remotepath, char *localpath) {
  if (remotepath == NULL) {
    fprintf(stderr, "Missing File\n");
    exit(EXIT_FAILURE);
  }

  char op = READ;
  xwrite_all(client_sd, &op, sizeof(char));

  receive_status(client_sd, "op");

  if (!oflag) {
    // If -o not specified, -f filepath will be used to write locally.
    localpath = xstrdup(remotepath);
  }

  char *localpath_cpy = xstrdup(localpath);

  // Send filepath/filename to server
  xwrite_all(client_sd, remotepath, BUFSIZE);

  char *dirpath = dirname(localpath_cpy);

  // If the -o filepath doesn't exist, create it
  if (mkdir_p(dirpath) < 0) {
    perror("creating file");
    exit(EXIT_FAILURE);
  }

  int fd;
  if ((fd = open(localpath, O_CREAT | O_WRONLY, FILEACC)) == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  // Check file exists on server
  receive_status(client_sd, "sent file");

  // Check File Size
  int f_size;
  xrecv(client_sd, &f_size, sizeof(int));

  // Get File data
  char *data;

  FILE *fp = fopen(localpath, "w");

  data = xmalloc(f_size + 1);

  int t = xrecv(client_sd, data, f_size);
  data[t] = '\0';

  fputs(data, fp);
  if (fp != NULL) {
    fclose(fp);
  }

  //////////////
  // Free Memory
  free(localpath_cpy);
  free(data);
  //////////////
}

void ls_from_server(int client_sd, char *remotepath) {
  if (remotepath == NULL) {
    fprintf(stderr, "Missing File\n");
    exit(EXIT_FAILURE);
  }

  char op = LIST;
  xwrite_all(client_sd, &op, sizeof(char));

  receive_status(client_sd, "op");

  // Send filepath/filename to server
  xwrite_all(client_sd, remotepath, BUFSIZE);

  // Check ls can be performed on given -f path
  receive_status(client_sd, "path opened");

  // Get buffer size
  int ls_size;
  xrecv(client_sd, &ls_size, sizeof(int));

  // Recv the ls output from the server
  char *ls_output = xmalloc(ls_size);
  if (xrecv(client_sd, ls_output, ls_size) < 0) {
    perror("");
    return;
  }

  printf("Server says\n%s", ls_output);
}

void receive_status(int client_sd, char *message) {
  int status;
  xrecv(client_sd, &status, sizeof(int));

  // stdout statuses (no exit)
  if (status == OK || status == CREATED) {
    fprintf(stdout, "%d: %s\n", status, message);
    return;
  }

  // stderr statuses (exit)
  if (status == BADREQ || status == NOTFOUND || status == SERVERERROR ||
      status == LOCKERR || status == STATERR) {
    fprintf(stderr, "%d: %s\n", status, message);
    exit(EXIT_FAILURE);
  }
}
