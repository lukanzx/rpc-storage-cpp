#ifndef SKIP_LIST_ON_RAFT_CLERK_H
#define SKIP_LIST_ON_RAFT_CLERK_H
#include "kvServerRPC.pb.h"
#include "mprpcconfig.h"
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <raftServerRpcUtil.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
class Clerk {
private:
  std::vector<std::shared_ptr<raftServerRpcUtil>> m_servers;
  std::string m_clientId;
  int m_requestId;
  int m_recentLeaderId;

  std::string Uuid() {
    return std::to_string(rand()) + std::to_string(rand()) +
           std::to_string(rand()) + std::to_string(rand());
  }

  void PutAppend(std::string key, std::string value, std::string op);

public:
  void Init(std::string configFileName);
  std::string Get(std::string key);

  void Put(std::string key, std::string value);
  void Append(std::string key, std::string value);

public:
  Clerk();
};

#endif
