#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static void do_something(int fd) {
  char rbuf[64] = {};
  int rv = read(fd, rbuf, sizeof(rbuf) - 1);
  if (rv < 0) {
    msg("read() error");
    return;
  }
  printf("client says: %s\n", rbuf);

  char wbuf[] = "world";
  rv = write(fd, wbuf, strlen(wbuf));
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  // needed in most applications
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  // bind
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(12345);
  addr.sin_addr.s_addr = INADDR_ANY;
  int rv = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (rv < 0) {
    die("bind()");
  }

  // listen
  rv = listen(fd, SOMAXCONN);
  if (rv < 0) {
    die("listen()");
  }

  while (true) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
      msg("accept()");
      continue;
    }

    do_something(connfd);
    close(connfd);
  }

  return 0;
}
