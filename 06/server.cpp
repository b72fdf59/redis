#include <arpa/inet.h>
#include <assert.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/ip.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define MAX_EVENTS 64

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    die("fcntl error");
  }
}

const size_t k_max_msg = 4096;

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2, // mark the connection for deletion
};

struct Conn {
  int fd = -1;
  uint32_t state = 0; // either STATE_REQ or STATE_RES
  // buffer for reading
  size_t rbuf_size = 0;
  size_t rbuf_read = 0;
  uint8_t rbuf[4 + k_max_msg];
  // buffer for writing
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  // accept
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg("accept() error");
    return -1; // error
  }

  // set the new connection fd to nonblocking mode
  fd_set_nb(connfd);
  // creating the struct Conn
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->rbuf_read = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}

static void state_req(Conn *conn);
static void state_res(Conn *conn);

static bool try_one_request(Conn *conn) {
  // try to parse a request from the buffer
  if (conn->rbuf_size < 4) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[conn->rbuf_read], 4);
  if (len > k_max_msg) {
    msg("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }

  // got one request, do something with it
  printf("client says: %.*s\n", len, &conn->rbuf[conn->rbuf_read + 4]);

  // generating echoing response
  memcpy(&conn->wbuf[0], &len, 4);
  memcpy(&conn->wbuf[4], &conn->rbuf[conn->rbuf_read + 4], len);
  conn->wbuf_size = 4 + len;

  // remove the request from the buffer.
  // note: frequent memmove is inefficient.
  // note: need better handling for production code.
  conn->rbuf_read += 4 + len;
  conn->rbuf_size = conn->rbuf_size - 4 - len;

  // change state
  conn->state = STATE_RES;
  state_res(conn);

  // continue the outer loop if the request was fully processed
  return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
  // try to fill the buffer
  assert(conn->rbuf_size < sizeof(conn->rbuf));

  if (conn->rbuf_size) {
    memmove(conn->rbuf, &conn->rbuf[conn->rbuf_read], conn->rbuf_size);
  }
  // reset read buffer
  conn->rbuf_read = 0;
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);
  if (rv < 0 && errno == EAGAIN) {
    // got EAGAIN, stop.
    return false;
  }
  if (rv < 0) {
    msg("read() error");
    conn->state = STATE_END;
    return false;
  }
  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      msg("unexpected EOF");
    } else {
      msg("EOF");
    }
    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf));

  // Try to process requests one by one.
  // Why is there a loop? Please read the explanation of "pipelining".
  while (try_one_request(conn)) {
  }
  return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}

static bool try_flush_buffer(Conn *conn) {
  ssize_t rv = 0;
  do {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  } while (rv < 0 && errno == EINTR);
  if (rv < 0 && errno == EAGAIN) {
    // got EAGAIN, stop.
    return false;
  }
  if (rv < 0) {
    msg("write() error");
    conn->state = STATE_END;
    return false;
  }
  conn->wbuf_sent += (size_t)rv;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  if (conn->wbuf_sent == conn->wbuf_size) {
    // response was fully sent, change state back
    conn->state = STATE_REQ;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  // still got some data in wbuf, could try to write again
  return true;
}

static void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {
  }
}

static void connection_io(Conn *conn) {
  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0); // not expected
  }
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  // bind
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0
  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rv) {
    die("bind()");
  }

  // listen
  rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  // a map of all client connections, keyed by fd
  std::vector<Conn *> fd2conn;

  // set the listen fd to nonblocking mode
  fd_set_nb(fd);

  // create epoll
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    die("epoll()");
  }

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = fd;
  rv = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
  if (rv < 0) {
    die("epoll_ctl(add)");
  }

  std::vector<struct epoll_event> epoll_events(MAX_EVENTS);

  // the event loop
  while (true) {
    // connection fds
    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }

      errno = 0;
      struct epoll_event event;
      event.data.fd = conn->fd;
      event.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      event.events = event.events | POLLERR;
      int rv = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &event);
      if (rv < 0) {
        if (errno == ENOENT) {
          rv = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &event);
          if (rv < 0) {
            die("epoll(add)");
          }
        } else {
          die("epoll(mod)");
        }
      }
    }

    // poll for active fds
    // the timeout argument doesn't matter here
    ssize_t nfds =
        epoll_wait(epoll_fd, epoll_events.data(), epoll_events.size(), -1);
    if (nfds < 0) {
      die("poll");
    }

    // process active connections
    for (size_t i = 0; i < (size_t)nfds; ++i) {
      // try to accept a new connection if the listening fd is active
      if (epoll_events[i].data.fd == fd) {
        (void)accept_new_conn(fd2conn, fd);
        continue;
      }

      Conn *conn = fd2conn[epoll_events[i].data.fd];
      connection_io(conn);
      if (conn->state == STATE_END) {
        // client closed normally, or something bad happened.
        // destroy this connection
        rv = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
        if (rv < 0) {
          die("epoll_ctl(del)");
        }
        fd2conn[conn->fd] = NULL;
        (void)close(conn->fd);
        free(conn);
      }
    }
  }

  if (close(epoll_fd)) {
    die("close() epoll");
  }
  return 0;
}
