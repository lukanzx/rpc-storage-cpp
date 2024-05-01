#ifndef SKIP_LIST_ON_RAFT_KVSERVER_H
#define SKIP_LIST_ON_RAFT_KVSERVER_H

#include "kvServerRPC.pb.h"
#include "raft.h"
#include "skipList.h"
#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <mutex>
#include <unordered_map>

class KvServer : raftKVRpcProctoc::kvServerRpc {
private:
  std::mutex m_mtx;
  int m_me;
  std::shared_ptr<Raft> m_raftNode;
  std::shared_ptr<LockQueue<ApplyMsg>> applyChan;
  int m_maxRaftState;

  std::string m_serializedKVData;
  SkipList<std::string, std::string> m_skipList;
  std::unordered_map<std::string, std::string> m_kvDB;

  std::unordered_map<int, LockQueue<Op> *> waitApplyCh;

  std::unordered_map<std::string, int> m_lastRequestId;

  int m_lastSnapShotRaftLogIndex;

public:
  KvServer() = delete;

  KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port);

  void StartKVServer();

  void DprintfKVDB();

  void ExecuteAppendOpOnKVDB(Op op);

  void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);

  void ExecutePutOpOnKVDB(Op op);

  void Get(const raftKVRpcProctoc::GetArgs *args,
           raftKVRpcProctoc::GetReply *reply);
  /**
   * 從raft節點中獲取消息  （不要誤以爲是執行【GET】命令）
   * @param message
   */
  void GetCommandFromRaft(ApplyMsg message);

  bool ifRequestDuplicate(std::string ClientId, int RequestId);

  void PutAppend(const raftKVRpcProctoc::PutAppendArgs *args,
                 raftKVRpcProctoc::PutAppendReply *reply);

  void ReadRaftApplyCommandLoop();

  void ReadSnapShotToInstall(std::string snapshot);

  bool SendMessageToWaitChan(const Op &op, int raftIndex);

  void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);

  void GetSnapShotFromRaft(ApplyMsg message);

  std::string MakeSnapShot();

public:
  void PutAppend(google::protobuf::RpcController *controller,
                 const ::raftKVRpcProctoc::PutAppendArgs *request,
                 ::raftKVRpcProctoc::PutAppendReply *response,
                 ::google::protobuf::Closure *done) override;

  void Get(google::protobuf::RpcController *controller,
           const ::raftKVRpcProctoc::GetArgs *request,
           ::raftKVRpcProctoc::GetReply *response,
           ::google::protobuf::Closure *done) override;

private:
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    ar & m_serializedKVData;

    ar & m_lastRequestId;
  }

  std::string getSnapshotData() {
    m_serializedKVData = m_skipList.dump_file();
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << *this;
    m_serializedKVData.clear();
    return ss.str();
  }

  void parseFromString(const std::string &str) {
    std::stringstream ss(str);
    boost::archive::text_iarchive ia(ss);
    ia >> *this;
    m_skipList.load_file(m_serializedKVData);
    m_serializedKVData.clear();
  }
};

#endif
