#ifndef RAFTSERVERRPC_H
#define RAFTSERVERRPC_H

#include "kvServerRPC.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"
#include <iostream>

class raftServerRpcUtil {
private:
  raftKVRpcProctoc::kvServerRpc_Stub *stub;

public:
  bool Get(raftKVRpcProctoc::GetArgs *GetArgs,
           raftKVRpcProctoc::GetReply *reply);
  bool PutAppend(raftKVRpcProctoc::PutAppendArgs *args,
                 raftKVRpcProctoc::PutAppendReply *reply);

  raftServerRpcUtil(std::string ip, short port);
  ~raftServerRpcUtil();
};

#endif
