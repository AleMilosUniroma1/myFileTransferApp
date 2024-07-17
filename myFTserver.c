#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/sendfile.h>
#include <unistd.h>

#include "myFTserver.h"

int aflag, pflag, dflag;
char *ft_root_dir_pathname = NULL;

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);

  int opt;
  uint16_t server_port;
  DIR *ft_root_dir = NULL;
  char *server_address = NULL;

  while ((opt = getopt(argc, argv, "a:p:d:h")) != -1) {
    switch (opt) {
    case 'a':
      server_address = xstrdup(optarg);
      aflag = 1;
      break;

    case 'p':
      // Check positivity and make sure sscanf doesn't fail
      if (isdigit(optarg[0]) != 0 &&
          sscanf(optarg, "%" SCNu16, &server_port) != 0) {
        pflag = 1;
      }
      break;

    case 'd':
      ft_root_dir_pathname = xstrdup(optarg);
      dflag = 1;
      break;

    case 'h':
    default:
      printf("usage: %s [-a address] [-p port]\n", argv[0]);
      exit(EXIT_FAILURE);
      break;
    }
  }

  if (!aflag || !pflag || !dflag) {
    printf("usage: %s [-a address] [-p port] [-d ft_root_dir]\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if (get_or_create_ft_root_directory(ft_root_dir_pathname, &ft_root_dir) < 0) {
    fprintf(stderr, "cannot set %s as root directory\n", ft_root_dir_pathname);
    exit(EXIT_FAILURE);
  } else {
    printf("%s set as Root Directory\n", ft_root_dir_pathname);
  }

  int server_sd;
  if ((server_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  };

  // Make reconnection to same port available
  int optval = 1;
  if (setsockopt(server_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) <
      0) {
    perror("setsockopt(SO_REUSEADDR) failed");
  }

  struct sockaddr_in server;

  server.sin_addr.s_addr = inet_addr(server_address);
  server.sin_family = AF_INET;
  server.sin_port = htons(server_port);

  if (bind(server_sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(server_sd, MAX_QUEUE) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on %s:%d\n", server_address, server_port);

  int client_sd;
  struct sockaddr client;
  socklen_t size_sockaddr = (socklen_t)sizeof(struct sockaddr);

  while ((client_sd = accept(server_sd, &client, &size_sockaddr)) >= 0) {
    printf("Socket %d connected\n", client_sd);

    pthread_t tid;

    pthread_create(&tid, NULL, handle_connection,
                   (void *)((size_t)((unsigned int)client_sd)));
  }

  close(server_sd);
  free(server_address);
  free(ft_root_dir_pathname);
  free(ft_root_dir);
}

int get_or_create_ft_root_directory(char *path, DIR **dir) {
  // If path == NULL || "" -> Invalid path, return -1
  if (path == NULL || dir == NULL) {
    return -1;
  }

  // Try to open directory
  *dir = opendir(path);

  if (ENOENT == errno) {
    // Try to create it
    if (mkdir(path, DIRACC) == 0) {
      *dir = opendir(path);
      return 0;
    } else {
      return -1;
    }
  }
  return 0;
}

void *handle_connection(void *arg) {
  char op;

  int client_sd = (int)((unsigned int)((size_t)arg));

  xrecv(client_sd, &op, sizeof(char));

  // 1. Find the OP that client wants to perform.
  // char op = request[0];
  if (op != READ && op != WRITE && op != LIST) {
    notify_status(client_sd, BADREQ);
    return NULL;
  } else {
    notify_status(client_sd, OK);
  }

  char filepath[BUFSIZE];
  xrecv(client_sd, filepath, BUFSIZE);

  // 3. Call the handler for that OP.
  if (op == READ) {
    handle_read(client_sd, filepath);
  }
  if (op == WRITE) {
    handle_write(client_sd, filepath);
  }
  if (op == LIST) {
    handle_ls(client_sd, filepath);
  }

  close(client_sd);
  return NULL;
}

//////////////////////////////////////////////////////////////////////
// Handle Write
//////////////////////////////////////////////////////////////////////
void handle_write(int client_sd, char *write_path) {
  char fullpath[BUFSIZE];
  sprintf(fullpath, "%s/%s", ft_root_dir_pathname, write_path);

  char *fullpath_cpy = xstrdup(fullpath);

  int fd = open(fullpath, O_WRONLY);
  if (fd >= 0) {
    notify_status(client_sd, OK);
  } else {
    // The file needs to be created, so client can write on it.
    char *prefixpath = dirname(fullpath_cpy);

    if (mkdir_p(prefixpath) == 0) {

      fd = open(fullpath, O_WRONLY | O_CREAT, FILEACC);
      if (fd < 0) {
        notify_status(client_sd, SERVERERROR);
        goto Exit;
      }

      if (flock(fd, LOCK_EX) == -1) {
        notify_status(client_sd, LOCKERR);
        goto Exit;
      }

      if (ftruncate(fd, 0)) {
        notify_status(client_sd, SERVERERROR);
        goto Exit;
      }

      // Creation success
      notify_status(client_sd, CREATED);

    } else {
      // Creation fail
      notify_status(client_sd, SERVERERROR);
      goto Exit;
    }
  }

  // Receive the file_size to write.
  int f_size;
  xrecv(client_sd, &f_size, sizeof(int));

  // Get File data
  char *data;

  FILE *fp = fopen(fullpath, "w");

  data = xmalloc(f_size + 1);

  int t = xrecv(client_sd, data, f_size);
  data[t] = '\0';

  fputs(data, fp);

//////////////
// Free Memory
Exit:
  if (fp != NULL) {
    fclose(fp);
  }
  close(fd);
  free(fullpath_cpy);
  free(data);
  //////////////
}

//////////////////////////////////////////////////////////////////////
// Handle Read
//////////////////////////////////////////////////////////////////////
void handle_read(int client_sd, char *read_path) {
  char fullpath[BUFSIZE];
  sprintf(fullpath, "%s/%s", ft_root_dir_pathname, read_path);

  int fd = open(fullpath, O_RDONLY);
  if (fd < 0) {
    // the file doesn't exist or cannot be accessed. Client Cannot read.
    notify_status(client_sd, NOTFOUND);
    return;
  }

  if (flock(fd, LOCK_SH) == -1) {
    notify_status(client_sd, LOCKERR);
    goto Exit;
  }

  notify_status(client_sd, OK);

  struct stat obj;
  if (stat(fullpath, &obj) == -1) {
    notify_status(client_sd, STATERR);
    goto Exit;
  }

  int f_size = obj.st_size;

  // Send the file size to the client
  xwrite_all(client_sd, &f_size, sizeof(int));

  // Send data
  sendfile(client_sd, fd, NULL, f_size);

Exit:
  if (fd != -1)
    close(fd);
}

//////////////////////////////////////////////////////////////////////
// Handle List
//////////////////////////////////////////////////////////////////////
void handle_ls(int client_sd, char *path) {
  char fullpath[BUFSIZE];
  sprintf(fullpath, "%s/%s", ft_root_dir_pathname, path);

  char *buffer = NULL;
  if (access(fullpath, R_OK) == 0) {

    if (ls_la(fullpath, &buffer) < 0) {
      notify_status(client_sd, SERVERERROR);
      goto Exit;
    }

    notify_status(client_sd, OK);

    // Send buf_siz to client
    int buf_siz = strlen(buffer);
    xwrite_all(client_sd, &buf_siz, sizeof(int));

    // Send the ls output to the client
    xwrite_all(client_sd, buffer, buf_siz);

  } else {
    notify_status(client_sd, NOTFOUND);
  }

Exit:
  // Cleanup
  free(buffer);
}

void notify_status(int client_sd, int status) {
  xwrite_all(client_sd, &status, sizeof(int));
}
