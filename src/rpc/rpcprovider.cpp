#include "rpcprovider.h"
#include "rpcheader.pb.h"
#include "util.h"
#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <netdb.h>
#include <string>
#include <unistd.h>

void RpcProvider::NotifyService(google::protobuf::Service *service) {
  ServiceInfo service_info;

  const google::protobuf::ServiceDescriptor *pserviceDesc =
      service->GetDescriptor();

  std::string service_name = pserviceDesc->name();

  int methodCnt = pserviceDesc->method_count();

  std::cout << "service_name:" << service_name << std::endl;

  for (int i = 0; i < methodCnt; ++i) {

    const google::protobuf::MethodDescriptor *pmethodDesc =
        pserviceDesc->method(i);
    std::string method_name = pmethodDesc->name();
    service_info.m_methodMap.insert({method_name, pmethodDesc});
  }
  service_info.m_service = service;
  m_serviceMap.insert({service_name, service_info});
}

void RpcProvider::Run(int nodeIndex, short port) {

  char *ipC;
  char hname[128];
  struct hostent *hent;
  gethostname(hname, sizeof(hname));
  hent = gethostbyname(hname);
  for (int i = 0; hent->h_addr_list[i]; i++) {
    ipC = inet_ntoa(*(struct in_addr *)(hent->h_addr_list[i]));
  }
  std::string ip = std::string(ipC);

  std::string node = "node" + std::to_string(nodeIndex);
  std::ofstream outfile;
  outfile.open("test.conf", std::ios::app);
  if (!outfile.is_open()) {
    std::cout << "打开文件失败！" << std::endl;
    exit(EXIT_FAILURE);
  }
  outfile << node + "ip=" + ip << std::endl;
  outfile << node + "port=" + std::to_string(port) << std::endl;
  outfile.close();

  muduo::net::InetAddress address(ip, port);

  m_muduo_server = std::make_shared<muduo::net::TcpServer>(
      &m_eventLoop, address, "RpcProvider");

  /*
  bind的作用：
  如果不使用std::bind将回调函数和TcpConnection对象绑定起来，那么在回调函数中就无法直接访问和修改TcpConnection对象的状态。因为回调函数是作为一个独立的函数被调用的，它没有当前对象的上下文信息（即this指针），也就无法直接访问当前对象的状态。
  如果要在回调函数中访问和修改TcpConnection对象的状态，需要通过参数的形式将当前对象的指针传递进去，并且保证回调函数在当前对象的上下文环境中被调用。这种方式比较复杂，容易出错，也不便于代码的编写和维护。因此，使用std::bind将回调函数和TcpConnection对象绑定起来，可以更加方便、直观地访问和修改对象的状态，同时也可以避免一些常见的错误。
  */
  m_muduo_server->setConnectionCallback(
      std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
  m_muduo_server->setMessageCallback(
      std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));

  m_muduo_server->setThreadNum(4);

  std::cout << "RpcProvider start service at ip:" << ip << " port:" << port
            << std::endl;

  m_muduo_server->start();
  m_eventLoop.loop();
  /*
  这段代码是在启动网络服务和事件循环，其中server是一个TcpServer对象，m_eventLoop是一个EventLoop对象。

首先调用server.start()函数启动网络服务。在Muduo库中，TcpServer类封装了底层网络操作，包括TCP连接的建立和关闭、接收客户端数据、发送数据给客户端等等。通过调用TcpServer对象的start函数，可以启动底层网络服务并监听客户端连接的到来。

接下来调用m_eventLoop.loop()函数启动事件循环。在Muduo库中，EventLoop类封装了事件循环的核心逻辑，包括定时器、IO事件、信号等等。通过调用EventLoop对象的loop函数，可以启动事件循环，等待事件的到来并处理事件。

在这段代码中，首先启动网络服务，然后进入事件循环阶段，等待并处理各种事件。网络服务和事件循环是两个相对独立的模块，它们的启动顺序和调用方式都是确定的。启动网络服务通常是在事件循环之前，因为网络服务是事件循环的基础。启动事件循环则是整个应用程序的核心，所有的事件都在事件循环中被处理。
  */
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn) {

  if (!conn->connected()) {

    conn->shutdown();
  }
}

/*
在框架内部，RpcProvider和RpcConsumer协商好之间通信用的protobuf数据类型
service_name method_name args
定义proto的message类型，进行数据头的序列化和反序列化 service_name method_name
args_size 16UserServiceLoginzhang san123456

header_size(4个字节) + header_str + args_str
10 "10"
10000 "1000000"
std::string   insert和copy方法
*/

void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer, muduo::Timestamp) {

  std::string recv_buf = buffer->retrieveAllAsString();

  google::protobuf::io::ArrayInputStream array_input(recv_buf.data(),
                                                     recv_buf.size());
  google::protobuf::io::CodedInputStream coded_input(&array_input);
  uint32_t header_size{};

  coded_input.ReadVarint32(&header_size);

  std::string rpc_header_str;
  RPC::RpcHeader rpcHeader;
  std::string service_name;
  std::string method_name;

  google::protobuf::io::CodedInputStream::Limit msg_limit =
      coded_input.PushLimit(header_size);
  coded_input.ReadString(&rpc_header_str, header_size);

  coded_input.PopLimit(msg_limit);
  uint32_t args_size{};
  if (rpcHeader.ParseFromString(rpc_header_str)) {

    service_name = rpcHeader.service_name();
    method_name = rpcHeader.method_name();
    args_size = rpcHeader.args_size();
  } else {

    std::cout << "rpc_header_str:" << rpc_header_str << " parse error!"
              << std::endl;
    return;
  }

  std::string args_str;

  bool read_args_success = coded_input.ReadString(&args_str, args_size);

  if (!read_args_success) {

    return;
  }

  auto it = m_serviceMap.find(service_name);
  if (it == m_serviceMap.end()) {
    std::cout << "服务：" << service_name << " is not exist!" << std::endl;
    std::cout << "当前已经有的服务列表为:";
    for (auto item : m_serviceMap) {
      std::cout << item.first << " ";
    }
    std::cout << std::endl;
    return;
  }

  auto mit = it->second.m_methodMap.find(method_name);
  if (mit == it->second.m_methodMap.end()) {
    std::cout << service_name << ":" << method_name << " is not exist!"
              << std::endl;
    return;
  }

  google::protobuf::Service *service = it->second.m_service;
  const google::protobuf::MethodDescriptor *method = mit->second;

  google::protobuf::Message *request =
      service->GetRequestPrototype(method).New();
  if (!request->ParseFromString(args_str)) {
    std::cout << "request parse error, content:" << args_str << std::endl;
    return;
  }
  google::protobuf::Message *response =
      service->GetResponsePrototype(method).New();

  google::protobuf::Closure *done =
      google::protobuf::NewCallback<RpcProvider,
                                    const muduo::net::TcpConnectionPtr &,
                                    google::protobuf::Message *>(
          this, &RpcProvider::SendRpcResponse, conn, response);

  /*
  为什么下面这个service->CallMethod
  要这么写？或者说为什么这么写就可以直接调用远程业务方法了
  这个service在运行的时候会是注册的service


  的纯虚函数CallMethod，而 .protoc生成的serviceRpc类 会根据传入参数自动调取
  生成的xx方法（如Login方法）， 由于xx方法被 用户注册的service类
  重写了，因此这个方法运行的时候会调用 用户注册的service类 的xx方法 真的是妙呀
  */

  service->CallMethod(method, nullptr, request, response, done);
}

void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn,
                                  google::protobuf::Message *response) {
  std::string response_str;
  if (response->SerializeToString(&response_str)) {

    conn->send(response_str);
  } else {
    std::cout << "serialize response_str error!" << std::endl;
  }
}

RpcProvider::~RpcProvider() {
  std::cout << "[func - RpcProvider::~RpcProvider()]: ip和port信息："
            << m_muduo_server->ipPort() << std::endl;
  m_eventLoop.quit();
}
