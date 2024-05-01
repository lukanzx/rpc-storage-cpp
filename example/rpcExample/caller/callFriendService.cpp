#include <iostream>

#include "rpcExample/friend.pb.h"

#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"

int main(int argc, char **argv) {

  std::string ip = "127.0.1.1";
  short port = 7788;

  fixbug::FiendServiceRpc_Stub stub(new MprpcChannel(ip, port, true));

  fixbug::GetFriendsListRequest request;
  request.set_userid(1000);

  fixbug::GetFriendsListResponse response;

  MprpcController controller;

  int count = 10;
  while (count--) {
    std::cout << " 倒数" << count << "次发起RPC请求" << std::endl;
    stub.GetFriendsList(&controller, &request, &response, nullptr);

    if (controller.Failed()) {
      std::cout << controller.ErrorText() << std::endl;
    } else {
      if (0 == response.result().errcode()) {
        std::cout << "rpc GetFriendsList response success!" << std::endl;
        int size = response.friends_size();
        for (int i = 0; i < size; i++) {
          std::cout << "index:" << (i + 1) << " name:" << response.friends(i)
                    << std::endl;
        }
      } else {

        std::cout << "rpc GetFriendsList response error : "
                  << response.result().errmsg() << std::endl;
      }
    }
    sleep(5);
  }
  return 0;
}
