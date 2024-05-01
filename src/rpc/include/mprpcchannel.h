#ifndef MPRPCCHANNEL_H
#define MPRPCCHANNEL_H

#include <algorithm>
#include <functional>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

class MprpcChannel : public google::protobuf::RpcChannel {
public:
  void CallMethod(const google::protobuf::MethodDescriptor *method,
                  google::protobuf::RpcController *controller,
                  const google::protobuf::Message *request,
                  google::protobuf::Message *response,
                  google::protobuf::Closure *done) override;
  MprpcChannel(string ip, short port, bool connectNow);

private:
  int m_clientFd;
  const std::string m_ip;
  const uint16_t m_port;

  bool newConnect(const char *ip, uint16_t port, string *errMsg);
};

#endif