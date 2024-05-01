#include "kvServer.h"
#include "mprpcconfig.h"
#include <rpcprovider.h>

void KvServer::DprintfKVDB() {
  if (!Debug) {
    return;
  }
  std::lock_guard<std::mutex> lg(m_mtx);
  DEFER { m_skipList.display_list(); };
}

void KvServer::ExecuteAppendOpOnKVDB(Op op) {

  m_mtx.lock();

  m_skipList.insert_set_element(op.Key, op.Value);

  m_lastRequestId[op.ClientId] = op.RequestId;
  m_mtx.unlock();

  DprintfKVDB();
}

void KvServer::ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist) {
  m_mtx.lock();
  *value = "";
  *exist = false;
  if (m_skipList.search_element(op.Key, *value)) {
    *exist = true;
  }

  m_lastRequestId[op.ClientId] = op.RequestId;
  m_mtx.unlock();

  if (*exist) {

  } else {
  }
  DprintfKVDB();
}

void KvServer::ExecutePutOpOnKVDB(Op op) {
  m_mtx.lock();
  m_skipList.insert_set_element(op.Key, op.Value);

  m_lastRequestId[op.ClientId] = op.RequestId;
  m_mtx.unlock();

  DprintfKVDB();
}

void KvServer::Get(const raftKVRpcProctoc::GetArgs *args,
                   raftKVRpcProctoc::GetReply *reply) {
  Op op;
  op.Operation = "Get";
  op.Key = args->key();
  op.Value = "";
  op.ClientId = args->clientid();
  op.RequestId = args->requestid();

  int raftIndex = -1;
  int _ = -1;
  bool isLeader = false;
  m_raftNode->Start(op, &raftIndex, &_, &isLeader);

  if (!isLeader) {
    reply->set_err(ErrWrongLeader);
    return;
  }

  m_mtx.lock();

  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
    waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
  }
  auto chForRaftIndex = waitApplyCh[raftIndex];
  m_mtx.unlock();
  Op raftCommitOp;

  if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {

    int _ = -1;
    bool isLeader = false;
    m_raftNode->GetState(&_, &isLeader);

    if (ifRequestDuplicate(op.ClientId, op.RequestId) && isLeader) {

      std::string value;
      bool exist = false;
      ExecuteGetOpOnKVDB(op, &value, &exist);
      if (exist) {
        reply->set_err(OK);
        reply->set_value(value);
      } else {
        reply->set_err(ErrNoKey);
        reply->set_value("");
      }
    } else {
      reply->set_err(ErrWrongLeader);
    }
  } else {

    if (raftCommitOp.ClientId == op.ClientId &&
        raftCommitOp.RequestId == op.RequestId) {
      std::string value;
      bool exist = false;
      ExecuteGetOpOnKVDB(op, &value, &exist);
      if (exist) {
        reply->set_err(OK);
        reply->set_value(value);
      } else {
        reply->set_err(ErrNoKey);
        reply->set_value("");
      }
    } else {
      reply->set_err(ErrWrongLeader);
    }
  }
  m_mtx.lock();
  auto tmp = waitApplyCh[raftIndex];
  waitApplyCh.erase(raftIndex);
  delete tmp;
  m_mtx.unlock();
}

void KvServer::GetCommandFromRaft(ApplyMsg message) {
  Op op;
  op.parseFromString(message.Command);

  DPrintf("[KvServer::GetCommandFromRaft-kvserver{%d}] , Got Command --> "
          "Index:{%d} , ClientId {%s}, RequestId {%d}, "
          "Opreation {%s}, Key :{%s}, Value :{%s}",
          m_me, message.CommandIndex, &op.ClientId, op.RequestId, &op.Operation,
          &op.Key, &op.Value);
  if (message.CommandIndex <= m_lastSnapShotRaftLogIndex) {
    return;
  }

  if (!ifRequestDuplicate(op.ClientId, op.RequestId)) {

    if (op.Operation == "Put") {
      ExecutePutOpOnKVDB(op);
    }
    if (op.Operation == "Append") {
      ExecuteAppendOpOnKVDB(op);
    }
  }

  if (m_maxRaftState != -1) {
    IfNeedToSendSnapShotCommand(message.CommandIndex, 9);
  }

  SendMessageToWaitChan(op, message.CommandIndex);
}

bool KvServer::ifRequestDuplicate(std::string ClientId, int RequestId) {
  std::lock_guard<std::mutex> lg(m_mtx);
  if (m_lastRequestId.find(ClientId) == m_lastRequestId.end()) {
    return false;
  }
  return RequestId <= m_lastRequestId[ClientId];
}

void KvServer::PutAppend(const raftKVRpcProctoc::PutAppendArgs *args,
                         raftKVRpcProctoc::PutAppendReply *reply) {
  Op op;
  op.Operation = args->op();
  op.Key = args->key();
  op.Value = args->value();
  op.ClientId = args->clientid();
  op.RequestId = args->requestid();
  int raftIndex = -1;
  int _ = -1;
  bool isleader = false;

  m_raftNode->Start(op, &raftIndex, &_, &isleader);

  if (!isleader) {
    DPrintf("[func -KvServer::PutAppend -kvserver{%d}]From Client %s (Request "
            "%d) To Server %d, key %s, raftIndex %d , but "
            "not leader",
            m_me, &args->clientid(), args->requestid(), m_me, &op.Key,
            raftIndex);

    reply->set_err(ErrWrongLeader);
    return;
  }
  DPrintf("[func -KvServer::PutAppend -kvserver{%d}]From Client %s (Request "
          "%d) To Server %d, key %s, raftIndex %d , is "
          "leader ",
          m_me, &args->clientid(), args->requestid(), m_me, &op.Key, raftIndex);
  m_mtx.lock();
  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
    waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
  }
  auto chForRaftIndex = waitApplyCh[raftIndex];

  m_mtx.unlock();

  Op raftCommitOp;

  if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {
    DPrintf("[func -KvServer::PutAppend -kvserver{%d}]TIMEOUT PUTAPPEND !!!! "
            "Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %s, Opreation %s Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation,
            &op.Key, &op.Value);

    if (ifRequestDuplicate(op.ClientId, op.RequestId)) {
      reply->set_err(OK);
    } else {
      reply->set_err(ErrWrongLeader);
    }
  } else {
    DPrintf("[func -KvServer::PutAppend "
            "-kvserver{%d}]WaitChanGetRaftApplyMessage<--Server %d , get "
            "Command <-- Index:%d , "
            "ClientId %s, RequestId %d, Opreation %s, Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation,
            &op.Key, &op.Value);
    if (raftCommitOp.ClientId == op.ClientId &&
        raftCommitOp.RequestId == op.RequestId) {

      reply->set_err(OK);
    } else {
      reply->set_err(ErrWrongLeader);
    }
  }

  m_mtx.lock();

  auto tmp = waitApplyCh[raftIndex];
  waitApplyCh.erase(raftIndex);
  delete tmp;
  m_mtx.unlock();
}

void KvServer::ReadRaftApplyCommandLoop() {
  while (true) {

    auto message = applyChan->Pop();
    DPrintf("---------------tmp-------------[func-KvServer::"
            "ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了下raft的消息",
            m_me);

    if (message.CommandValid) {
      GetCommandFromRaft(message);
    }
    if (message.SnapshotValid) {
      GetSnapShotFromRaft(message);
    }
  }
}

void KvServer::ReadSnapShotToInstall(std::string snapshot) {
  if (snapshot.empty()) {

    return;
  }
  parseFromString(snapshot);
}

bool KvServer::SendMessageToWaitChan(const Op &op, int raftIndex) {
  std::lock_guard<std::mutex> lg(m_mtx);
  DPrintf("[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command "
          "--> Index:{%d} , ClientId {%d}, RequestId "
          "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
          m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key,
          &op.Value);

  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
    return false;
  }
  waitApplyCh[raftIndex]->Push(op);
  DPrintf("[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command "
          "--> Index:{%d} , ClientId {%d}, RequestId "
          "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
          m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key,
          &op.Value);
  return true;
}

void KvServer::IfNeedToSendSnapShotCommand(int raftIndex, int proportion) {
  if (m_raftNode->GetRaftStateSize() > m_maxRaftState / 10.0) {

    auto snapshot = MakeSnapShot();
    m_raftNode->Snapshot(raftIndex, snapshot);
  }
}

void KvServer::GetSnapShotFromRaft(ApplyMsg message) {
  std::lock_guard<std::mutex> lg(m_mtx);

  if (m_raftNode->CondInstallSnapshot(
          message.SnapshotTerm, message.SnapshotIndex, message.Snapshot)) {
    ReadSnapShotToInstall(message.Snapshot);
    m_lastSnapShotRaftLogIndex = message.SnapshotIndex;
  }
}

std::string KvServer::MakeSnapShot() {
  std::lock_guard<std::mutex> lg(m_mtx);
  std::string snapshotData = getSnapshotData();
  return snapshotData;
}

void KvServer::PutAppend(google::protobuf::RpcController *controller,
                         const ::raftKVRpcProctoc::PutAppendArgs *request,
                         ::raftKVRpcProctoc::PutAppendReply *response,
                         ::google::protobuf::Closure *done) {
  KvServer::PutAppend(request, response);
  done->Run();
}

void KvServer::Get(google::protobuf::RpcController *controller,
                   const ::raftKVRpcProctoc::GetArgs *request,
                   ::raftKVRpcProctoc::GetReply *response,
                   ::google::protobuf::Closure *done) {
  KvServer::Get(request, response);
  done->Run();
}

KvServer::KvServer(int me, int maxraftstate, std::string nodeInforFileName,
                   short port)
    : m_skipList(6) {
  std::shared_ptr<Persister> persister = std::make_shared<Persister>(me);

  m_me = me;
  m_maxRaftState = maxraftstate;

  applyChan = std::make_shared<LockQueue<ApplyMsg>>();

  m_raftNode = std::make_shared<Raft>();

  std::thread t([this, port]() -> void {
    RpcProvider provider;
    provider.NotifyService(this);
    provider.NotifyService(this->m_raftNode.get());

    provider.Run(m_me, port);
  });
  t.detach();

  std::cout << "raftServer node:" << m_me
            << " start to sleep to wait all ohter raftnode start!!!!"
            << std::endl;
  sleep(6);
  std::cout << "raftServer node:" << m_me
            << " wake up!!!! start to connect other raftnode" << std::endl;

  MprpcConfig config;
  config.LoadConfigFile(nodeInforFileName.c_str());
  std::vector<std::pair<std::string, short>> ipPortVt;
  for (int i = 0; i < INT_MAX - 1; ++i) {
    std::string node = "node" + std::to_string(i);

    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port");
    if (nodeIp.empty()) {
      break;
    }
    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));
  }
  std::vector<std::shared_ptr<RaftRpcUtil>> servers;

  for (int i = 0; i < ipPortVt.size(); ++i) {
    if (i == m_me) {
      servers.push_back(nullptr);
      continue;
    }
    std::string otherNodeIp = ipPortVt[i].first;
    short otherNodePort = ipPortVt[i].second;
    auto *rpc = new RaftRpcUtil(otherNodeIp, otherNodePort);
    servers.push_back(std::shared_ptr<RaftRpcUtil>(rpc));

    std::cout << "node" << m_me << " 连接node" << i << "success!" << std::endl;
  }
  sleep(ipPortVt.size() - me);
  m_raftNode->init(servers, m_me, persister, applyChan);

  m_skipList;
  waitApplyCh;
  m_lastRequestId;
  m_lastSnapShotRaftLogIndex = 0;
  auto snapshot = persister->ReadSnapshot();
  if (!snapshot.empty()) {
    ReadSnapShotToInstall(snapshot);
  }
  std::thread t2(&KvServer::ReadRaftApplyCommandLoop, this);
  t2.join();
}
