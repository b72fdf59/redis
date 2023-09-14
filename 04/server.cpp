#include <arpa/inet.h>
#include <assert.h>
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

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv < 0) {
      return -1; // error or unexpected eof
    }

    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t write_all(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv < 0) {
      return -1; // error or unexpected eof
    }

    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

const size_t k_max_msg = 4096;

static int32_t one_request(int connfd) {
  // read header
  char rbuf[4 + k_max_msg] = {};
  errno = 0;
  int32_t err = read_full(connfd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      msg("EOF");
    } else {
      msg("read() error");
    }

    return err;
  }
  printf("client says: %s\n", rbuf);

  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  // read request
  err = read_full(connfd, &rbuf[4], len);
  if (err) {
    msg("read() error");
  }

  // do something
  rbuf[4 + len] = '\0';
  printf("client says: %s\n", &rbuf[4]);

  // reply using the same protocol
  const char reply[] = "world";
  char wbuf[4 + sizeof(reply)];
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], &len, 4);
  return write_all(connfd, wbuf, 4 + len);
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
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
      continue;
    }

    while (true) {
      int32_t err = one_request(connfd);
      if (err) {
        break;
      }
    }
  }

  return 0;
}
