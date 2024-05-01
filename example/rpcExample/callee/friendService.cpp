#include "rpcExample/friend.pb.h"
#include <iostream>
#include <mprpcchannel.h>
#include <string>

#include "rpcprovider.h"
#include <vector>

class FriendService : public fixbug::FiendServiceRpc {
public:
  std::vector<std::string> GetFriendsList(uint32_t userid) {
    std::cout << "local do GetFriendsList service! userid:" << userid
              << std::endl;
    std::vector<std::string> vec;
    vec.push_back("gao yang");
    vec.push_back("liu hong");
    vec.push_back("wang shuo");
    return vec;
  }

  void GetFriendsList(::google::protobuf::RpcController *controller,
                      const ::fixbug::GetFriendsListRequest *request,
                      ::fixbug::GetFriendsListResponse *response,
                      ::google::protobuf::Closure *done) {
    uint32_t userid = request->userid();
    std::vector<std::string> friendsList = GetFriendsList(userid);
    response->mutable_result()->set_errcode(0);
    response->mutable_result()->set_errmsg("");
    for (std::string &name : friendsList) {
      std::string *p = response->add_friends();
      *p = name;
    }
    done->Run();
  }
};

int main(int argc, char **argv) {
  std::string ip = "127.0.0.1";
  short port = 7788;
  auto stub =
      new fixbug::FiendServiceRpc_Stub(new MprpcChannel(ip, port, false));

  RpcProvider provider;
  provider.NotifyService(new FriendService());

  provider.Run(1, 7788);

  return 0;
}
