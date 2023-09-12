#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(12345);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  int rv = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (rv < 0) {
    die("connect()");
  }

  char wbuf[] = "hello";
  rv = write(fd, wbuf, strlen(wbuf));
  if (rv < 0) {
    die("write");
  }

  char rbuf[64] = {};
  ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    die("read");
  }
  printf("server says: %s\n", rbuf);
  close(fd);
  return 0;
}
