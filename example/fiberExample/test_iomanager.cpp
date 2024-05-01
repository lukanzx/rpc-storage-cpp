#include "monsoon.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int sockfd;
void watch_io_read();

void do_io_write() {
  std::cout << "write callback" << std::endl;
  int so_err;
  socklen_t len = size_t(so_err);
  getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_err, &len);
  if (so_err) {
    std::cout << "connect fail" << std::endl;
    return;
  }
  std::cout << "connect success" << std::endl;
}

void do_io_read() {
  std::cout << "read callback" << std::endl;
  char buf[1024] = {0};
  int readlen = 0;
  readlen = read(sockfd, buf, sizeof(buf));
  if (readlen > 0) {
    buf[readlen] = '\0';
    std::cout << "read " << readlen << " bytes, read: " << buf << std::endl;
  } else if (readlen == 0) {
    std::cout << "peer closed";
    close(sockfd);
    return;
  } else {
    std::cout << "err, errno=" << errno << ", errstr=" << strerror(errno)
              << std::endl;
    close(sockfd);
    return;
  }

  monsoon::IOManager::GetThis()->scheduler(watch_io_read);
}

void watch_io_read() {
  std::cout << "watch_io_read" << std::endl;
  monsoon::IOManager::GetThis()->addEvent(sockfd, monsoon::READ, do_io_read);
}

void test_io() {
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  monsoon::CondPanic(sockfd > 0, "scoket should >0");
  fcntl(sockfd, F_SETFL, O_NONBLOCK);

  sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(80);
  inet_pton(AF_INET, "36.152.44.96", &servaddr.sin_addr.s_addr);

  int rt = connect(sockfd, (const sockaddr *)&servaddr, sizeof(servaddr));
  if (rt != 0) {
    if (errno == EINPROGRESS) {
      std::cout << "EINPROGRESS" << std::endl;

      monsoon::IOManager::GetThis()->addEvent(sockfd, monsoon::WRITE,
                                              do_io_write);

      monsoon::IOManager::GetThis()->addEvent(sockfd, monsoon::READ,
                                              do_io_read);
    } else {
      std::cout << "connect error, errno:" << errno
                << ", errstr:" << strerror(errno) << std::endl;
    }
  } else {
    std::cout << "else, errno:" << errno << ", errstr:" << strerror(errno)
              << std::endl;
  }
}

void test_iomanager() {
  monsoon::IOManager iom;

  iom.scheduler(test_io);
}

int main(int argc, char *argv[]) {
  test_iomanager();

  return 0;
}