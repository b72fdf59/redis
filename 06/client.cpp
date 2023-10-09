#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const size_t k_max_msg = 4096;

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static int die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

// helpers
static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv < 0) {
      return -1;
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
      return -1;
    }

    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }

  return 0;
}

static int32_t query(int fd, const char *text) {
  uint32_t len = strlen(text);
  if (len > k_max_msg) {
    return -1;
  }

  char wbuf[4 + k_max_msg + 1];
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], text, len);
  int32_t err = write_all(fd, wbuf, len + 4);
  if (err) {
    return err;
  }

  // 4 byte header
  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      msg("EOF");
    } else {
      msg("read() error");
    }
    return err;
  }

  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  memcpy(&len, rbuf, 4);
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    msg("read() error");
    return err;
  }

  // do something
  rbuf[4 + len] = '\0';
  printf("server says: %s\n", &rbuf[4]);
  return 0;
}

static int32_t querymany(int fd, const char **texts, uint32_t num_texts) {
  char wbuf[(4 + k_max_msg + 1) * num_texts];
  uint32_t total_len = 0;
  size_t wbuf_pointer = 0;
  for (uint32_t i = 0; i < num_texts; i++) {
    const char *text = texts[i];

    uint32_t len = strlen(text);
    if (len > k_max_msg) {
      return -1;
    }

    total_len += len;
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
  }

  int32_t err = write_all(fd, wbuf, len + 4);
  if (err) {
    return err;
  }

  // 4 byte header
  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      msg("EOF");
    } else {
      msg("read() error");
    }
    return err;
  }

  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  memcpy(&len, rbuf, 4);
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    msg("read() error");
    return err;
  }

  // do something
  rbuf[4 + len] = '\0';
  printf("server says: %s\n", &rbuf[4]);

  return 0;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
    die("connect()");
  }

  // multiple requests
  int32_t err = query(fd, "hello1");
  if (err) {
    goto L_DONE;
  }

  err = query(fd, "hello2");
  if (err) {
    goto L_DONE;
  }

  err = query(fd, "hello3");
  if (err) {
    goto L_DONE;
  }

L_DONE:
  close(fd);
  return 0;
}
