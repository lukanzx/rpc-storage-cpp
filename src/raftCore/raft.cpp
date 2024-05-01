#include "raft.h"
#include "config.h"
#include "util.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>

void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args,
                          raftRpcProctoc::AppendEntriesReply *reply) {
  std::lock_guard<std::mutex> locker(m_mtx);
  reply->set_appstate(AppNormal);

  if (args->term() < m_currentTerm) {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(-100);
    DPrintf("[func-AppendEntries-rf{%d}] 拒绝了 因为Leader{%d}的term{%v}< "
            "rf{%d}.term{%d}\n",
            m_me, args->leaderid(), args->term(), m_me, m_currentTerm);
    return;
  }

  DEFER { persist(); };
  if (args->term() > m_currentTerm) {

    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;
  }
  myAssert(args->term() == m_currentTerm,
           format("assert {args.Term == rf.currentTerm} fail"));

  m_status = Follower;

  m_lastResetElectionTime = now();

  if (args->prevlogindex() > getLastLogIndex()) {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(getLastLogIndex() + 1);

    return;
  } else if (args->prevlogindex() < m_lastSnapshotIncludeIndex) {

    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(m_lastSnapshotIncludeIndex + 1);
  }

  if (matchLog(args->prevlogindex(), args->prevlogterm())) {

    for (int i = 0; i < args->entries_size(); i++) {
      auto log = args->entries(i);
      if (log.logindex() > getLastLogIndex()) {

        m_logs.push_back(log);
      } else {

        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() ==
                log.logterm() &&
            m_logs[getSlicesIndexFromLogIndex(log.logindex())].command() !=
                log.command()) {

          myAssert(
              false,
              format(
                  "[func-AppendEntries-rf{%d}] "
                  "两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                  " {%d:%d}却不同！！\n",
                  m_me, log.logindex(), log.logterm(), m_me,
                  m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(),
                  args->leaderid(), log.command()));
        }
        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() !=
            log.logterm()) {

          m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log;
        }
      }
    }

    myAssert(getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
             format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != "
                    "args.PrevLogIndex{%d}+len(args.Entries){%d}",
                    m_me, getLastLogIndex(), args->prevlogindex(),
                    args->entries_size()));

    if (args->leadercommit() > m_commitIndex) {
      m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());
    }

    myAssert(getLastLogIndex() >= m_commitIndex,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < "
                    "rf.commitIndex{%d}",
                    m_me, getLastLogIndex(), m_commitIndex));
    reply->set_success(true);
    reply->set_term(m_currentTerm);

    return;
  } else {

    reply->set_updatenextindex(args->prevlogindex());

    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex;
         --index) {
      if (getLogTermFromLogIndex(index) !=
          getLogTermFromLogIndex(args->prevlogindex())) {
        reply->set_updatenextindex(index + 1);
        break;
      }
    }
    reply->set_success(false);
    reply->set_term(m_currentTerm);

    return;
  }
}

void Raft::applierTicker() {
  while (true) {
    m_mtx.lock();
    if (m_status == Leader) {
      DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   "
              "m_commitIndex{%d}",
              m_me, m_lastApplied, m_commitIndex);
    }
    auto applyMsgs = getApplyLogs();
    m_mtx.unlock();

    if (!applyMsgs.empty()) {
      DPrintf("[func- Raft::applierTicker()-raft{%d}] "
              "向kvserver報告的applyMsgs長度爲：{%d}",
              m_me, applyMsgs.size());
    }
    for (auto &message : applyMsgs) {
      applyChan->Push(message);
    }

    sleepNMilliseconds(ApplyInterval);
  }
}

bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex,
                               std::string snapshot) {
  return true;
}

void Raft::doElection() {
  std::lock_guard<std::mutex> g(m_mtx);

  if (m_status == Leader) {
  }

  if (m_status != Leader) {
    DPrintf("[       ticker-func-rf(%d)              ]  "
            "选举定时器到期且不是leader，开始选举 \n",
            m_me);

    m_status = Candidate;

    m_currentTerm += 1;
    m_votedFor = m_me;
    persist();
    std::shared_ptr<int> votedNum = std::make_shared<int>(1);

    m_lastResetElectionTime = now();

    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        continue;
      }
      int lastLogIndex = -1, lastLogTerm = -1;
      getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm);

      std::shared_ptr<raftRpcProctoc::RequestVoteArgs> requestVoteArgs =
          std::make_shared<raftRpcProctoc::RequestVoteArgs>();
      requestVoteArgs->set_term(m_currentTerm);
      requestVoteArgs->set_candidateid(m_me);
      requestVoteArgs->set_lastlogindex(lastLogIndex);
      requestVoteArgs->set_lastlogterm(lastLogTerm);
      auto requestVoteReply =
          std::make_shared<raftRpcProctoc::RequestVoteReply>();

      std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs,
                    requestVoteReply, votedNum);
      t.detach();
    }
  }
}

void Raft::doHeartBeat() {
  std::lock_guard<std::mutex> g(m_mtx);

  if (m_status == Leader) {
    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] "
            "Leader的心跳定时器触发了且拿到mutex，开始发送AE\n",
            m_me);
    auto appendNums = std::make_shared<int>(1);

    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        continue;
      }
      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] "
              "Leader的心跳定时器触发了 index:{%d}\n",
              m_me, i);
      myAssert(m_nextIndex[i] >= 1,
               format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));

      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) {

        std::thread t(&Raft::leaderSendSnapShot, this, i);
        t.detach();
        continue;
      }

      int preLogIndex = -1;
      int PrevLogTerm = -1;
      getPrevLogInfo(i, &preLogIndex, &PrevLogTerm);
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs =
          std::make_shared<raftRpcProctoc::AppendEntriesArgs>();
      appendEntriesArgs->set_term(m_currentTerm);
      appendEntriesArgs->set_leaderid(m_me);
      appendEntriesArgs->set_prevlogindex(preLogIndex);
      appendEntriesArgs->set_prevlogterm(PrevLogTerm);
      appendEntriesArgs->clear_entries();
      appendEntriesArgs->set_leadercommit(m_commitIndex);
      if (preLogIndex != m_lastSnapshotIncludeIndex) {
        for (int j = getSlicesIndexFromLogIndex(preLogIndex) + 1;
             j < m_logs.size(); ++j) {
          raftRpcProctoc::LogEntry *sendEntryPtr =
              appendEntriesArgs->add_entries();
          *sendEntryPtr = m_logs[j];
        }
      } else {
        for (const auto &item : m_logs) {
          raftRpcProctoc::LogEntry *sendEntryPtr =
              appendEntriesArgs->add_entries();
          *sendEntryPtr = item;
        }
      }
      int lastLogIndex = getLastLogIndex();

      myAssert(appendEntriesArgs->prevlogindex() +
                       appendEntriesArgs->entries_size() ==
                   lastLogIndex,
               format("appendEntriesArgs.PrevLogIndex{%d}+len("
                      "appendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                      appendEntriesArgs->prevlogindex(),
                      appendEntriesArgs->entries_size(), lastLogIndex));

      const std::shared_ptr<raftRpcProctoc::AppendEntriesReply>
          appendEntriesReply =
              std::make_shared<raftRpcProctoc::AppendEntriesReply>();
      appendEntriesReply->set_appstate(Disconnected);

      std::thread t(&Raft::sendAppendEntries, this, i, appendEntriesArgs,
                    appendEntriesReply, appendNums);
      t.detach();
    }
    m_lastResetHearBeatTime = now();
  }
}

void Raft::electionTimeOutTicker() {

  while (true) {
    /**
     * 如果不睡眠，那么对于leader，这个函数会一直空转，浪费cpu。且加入协程之后，空转会导致其他协程无法运行，对于时间敏感的AE，会导致心跳无法正常发送导致异常
     */
    while (m_status == Leader) {
      usleep(HeartBeatTimeout);
    }
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>>
        suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      m_mtx.lock();
      wakeTime = now();
      suitableSleepTime =
          getRandomizedElectionTimeout() + m_lastResetElectionTime - wakeTime;
      m_mtx.unlock();
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() >
        1) {

      auto start = std::chrono::steady_clock::now();

      usleep(std::chrono::duration_cast<std::chrono::microseconds>(
                 suitableSleepTime)
                 .count());

      auto end = std::chrono::steady_clock::now();

      std::chrono::duration<double, std::milli> duration = end - start;

      std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       suitableSleepTime)
                       .count()
                << " 毫秒\033[0m" << std::endl;
      std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: "
                << duration.count() << " 毫秒\033[0m" << std::endl;
    }

    if (std::chrono::duration<double, std::milli>(m_lastResetElectionTime -
                                                  wakeTime)
            .count() > 0) {

      continue;
    }
    doElection();
  }
}

std::vector<ApplyMsg> Raft::getApplyLogs() {
  std::vector<ApplyMsg> applyMsgs;
  myAssert(
      m_commitIndex <= getLastLogIndex(),
      format("[func-getApplyLogs-rf{%d}] commitIndex{%d} >getLastLogIndex{%d}",
             m_me, m_commitIndex, getLastLogIndex()));

  while (m_lastApplied < m_commitIndex) {
    m_lastApplied++;
    myAssert(
        m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex() ==
            m_lastApplied,
        format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)]."
               "LogIndex{%d} != rf.lastApplied{%d} ",
               m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex(),
               m_lastApplied));
    ApplyMsg applyMsg;
    applyMsg.CommandValid = true;
    applyMsg.SnapshotValid = false;
    applyMsg.Command =
        m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].command();
    applyMsg.CommandIndex = m_lastApplied;
    applyMsgs.emplace_back(applyMsg);
  }
  return applyMsgs;
}

int Raft::getNewCommandIndex() {

  auto lastLogIndex = getLastLogIndex();
  return lastLogIndex + 1;
}

void Raft::getPrevLogInfo(int server, int *preIndex, int *preTerm) {

  if (m_nextIndex[server] == m_lastSnapshotIncludeIndex + 1) {

    *preIndex = m_lastSnapshotIncludeIndex;
    *preTerm = m_lastSnapshotIncludeTerm;
    return;
  }
  auto nextIndex = m_nextIndex[server];
  *preIndex = nextIndex - 1;
  *preTerm = m_logs[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

void Raft::GetState(int *term, bool *isLeader) {
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };

  *term = m_currentTerm;
  *isLeader = (m_status == Leader);
}

void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args,
                           raftRpcProctoc::InstallSnapshotResponse *reply) {
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };
  if (args->term() < m_currentTerm) {
    reply->set_term(m_currentTerm);

    return;
  }
  if (args->term() > m_currentTerm) {

    m_currentTerm = args->term();
    m_votedFor = -1;
    m_status = Follower;
    persist();
  }
  m_status = Follower;
  m_lastResetElectionTime = now();

  if (args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex) {

    return;
  }

  auto lastLogIndex = getLastLogIndex();

  if (lastLogIndex > args->lastsnapshotincludeindex()) {
    m_logs.erase(
        m_logs.begin(),
        m_logs.begin() +
            getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
  } else {
    m_logs.clear();
  }
  m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
  m_lastApplied = std::max(m_lastApplied, args->lastsnapshotincludeindex());
  m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
  m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

  reply->set_term(m_currentTerm);
  ApplyMsg msg;
  msg.SnapshotValid = true;
  msg.Snapshot = args->data();
  msg.SnapshotTerm = args->lastsnapshotincludeterm();
  msg.SnapshotIndex = args->lastsnapshotincludeindex();

  std::thread t(&Raft::pushMsgToKvServer, this, msg);
  t.detach();

  m_persister->Save(persistData(), args->data());
}

void Raft::pushMsgToKvServer(ApplyMsg msg) { applyChan->Push(msg); }

void Raft::leaderHearBeatTicker() {
  while (true) {

    while (m_status != Leader) {
      usleep(1000 * HeartBeatTimeout);
    }
    static std::atomic<int32_t> atomicCount = 0;

    std::chrono::duration<signed long int, std::ratio<1, 1000000000>>
        suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      std::lock_guard<std::mutex> lock(m_mtx);
      wakeTime = now();
      suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) +
                          m_lastResetHearBeatTime - wakeTime;
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() >
        1) {
      std::cout << atomicCount
                << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       suitableSleepTime)
                       .count()
                << " 毫秒\033[0m" << std::endl;

      auto start = std::chrono::steady_clock::now();

      usleep(std::chrono::duration_cast<std::chrono::microseconds>(
                 suitableSleepTime)
                 .count());

      auto end = std::chrono::steady_clock::now();

      std::chrono::duration<double, std::milli> duration = end - start;

      std::cout << atomicCount
                << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: "
                << duration.count() << " 毫秒\033[0m" << std::endl;
      ++atomicCount;
    }

    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime -
                                                  wakeTime)
            .count() > 0) {

      continue;
    }

    doHeartBeat();
  }
}

void Raft::leaderSendSnapShot(int server) {
  m_mtx.lock();
  raftRpcProctoc::InstallSnapshotRequest args;
  args.set_leaderid(m_me);
  args.set_term(m_currentTerm);
  args.set_lastsnapshotincludeindex(m_lastSnapshotIncludeIndex);
  args.set_lastsnapshotincludeterm(m_lastSnapshotIncludeTerm);
  args.set_data(m_persister->ReadSnapshot());

  raftRpcProctoc::InstallSnapshotResponse reply;
  m_mtx.unlock();
  bool ok = m_peers[server]->InstallSnapshot(&args, &reply);
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };
  if (!ok) {
    return;
  }
  if (m_status != Leader || m_currentTerm != args.term()) {
    return;
  }

  if (reply.term() > m_currentTerm) {

    m_currentTerm = reply.term();
    m_votedFor = -1;
    m_status = Follower;
    persist();
    m_lastResetElectionTime = now();
    return;
  }
  m_matchIndex[server] = args.lastsnapshotincludeindex();
  m_nextIndex[server] = m_matchIndex[server] + 1;
}

void Raft::leaderUpdateCommitIndex() {
  m_commitIndex = m_lastSnapshotIncludeIndex;

  for (int index = getLastLogIndex(); index >= m_lastSnapshotIncludeIndex + 1;
       index--) {
    int sum = 0;
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        sum += 1;
        continue;
      }
      if (m_matchIndex[i] >= index) {
        sum += 1;
      }
    }

    if (sum >= m_peers.size() / 2 + 1 &&
        getLogTermFromLogIndex(index) == m_currentTerm) {
      m_commitIndex = index;
      break;
    }
  }
}

bool Raft::matchLog(int logIndex, int logTerm) {
  myAssert(logIndex >= m_lastSnapshotIncludeIndex &&
               logIndex <= getLastLogIndex(),
           format("不满足：logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&"
                  "logIndex{%d}<=rf.getLastLogIndex{%d}",
                  logIndex, m_lastSnapshotIncludeIndex, logIndex,
                  getLastLogIndex()));
  return logTerm == getLogTermFromLogIndex(logIndex);
}

void Raft::persist() {

  auto data = persistData();
  m_persister->SaveRaftState(data);
}

void Raft::RequestVote(const raftRpcProctoc::RequestVoteArgs *args,
                       raftRpcProctoc::RequestVoteReply *reply) {
  std::lock_guard<std::mutex> lg(m_mtx);

  DEFER { persist(); };

  if (args->term() < m_currentTerm) {
    reply->set_term(m_currentTerm);
    reply->set_votestate(Expire);
    reply->set_votegranted(false);
    return;
  }

  if (args->term() > m_currentTerm) {

    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;
  }
  myAssert(
      args->term() == m_currentTerm,
      format("[func--rf{%d}] 前面校验过args.Term==rf.currentTerm，这里却不等",
             m_me));

  int lastLogTerm = getLastLogTerm();

  if (!UpToDate(args->lastlogindex(), args->lastlogterm())) {

    if (args->lastlogterm() < lastLogTerm) {

    } else {
    }
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);

    return;
  }

  if (m_votedFor != -1 && m_votedFor != args->candidateid()) {

    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);

    return;
  } else {
    m_votedFor = args->candidateid();
    m_lastResetElectionTime = now();

    reply->set_term(m_currentTerm);
    reply->set_votestate(Normal);
    reply->set_votegranted(true);

    return;
  }
}

bool Raft::UpToDate(int index, int term) {

  int lastIndex = -1;
  int lastTerm = -1;
  getLastLogIndexAndTerm(&lastIndex, &lastTerm);
  return term > lastTerm || (term == lastTerm && index >= lastIndex);
}

void Raft::getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm) {
  if (m_logs.empty()) {
    *lastLogIndex = m_lastSnapshotIncludeIndex;
    *lastLogTerm = m_lastSnapshotIncludeTerm;
    return;
  } else {
    *lastLogIndex = m_logs[m_logs.size() - 1].logindex();
    *lastLogTerm = m_logs[m_logs.size() - 1].logterm();
    return;
  }
}
/**
 *
 * @return 最新的log的logindex，即log的逻辑index。区别于log在m_logs中的物理index
 * 可见：getLastLogIndexAndTerm()
 */
int Raft::getLastLogIndex() {
  int lastLogIndex = -1;
  int _ = -1;
  getLastLogIndexAndTerm(&lastLogIndex, &_);
  return lastLogIndex;
}

int Raft::getLastLogTerm() {
  int _ = -1;
  int lastLogTerm = -1;
  getLastLogIndexAndTerm(&_, &lastLogTerm);
  return lastLogTerm;
}

/**
 *
 * @param logIndex log的逻辑index。注意区别于m_logs的物理index
 * @return
 */
int Raft::getLogTermFromLogIndex(int logIndex) {
  myAssert(logIndex >= m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} < "
                  "rf.lastSnapshotIncludeIndex{%d}",
                  m_me, logIndex, m_lastSnapshotIncludeIndex));

  int lastLogIndex = getLastLogIndex();

  myAssert(logIndex <= lastLogIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > "
                  "lastLogIndex{%d}",
                  m_me, logIndex, lastLogIndex));

  if (logIndex == m_lastSnapshotIncludeIndex) {
    return m_lastSnapshotIncludeTerm;
  } else {
    return m_logs[getSlicesIndexFromLogIndex(logIndex)].logterm();
  }
}

int Raft::GetRaftStateSize() { return m_persister->RaftStateSize(); }

int Raft::getSlicesIndexFromLogIndex(int logIndex) {
  myAssert(logIndex > m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= "
                  "rf.lastSnapshotIncludeIndex{%d}",
                  m_me, logIndex, m_lastSnapshotIncludeIndex));
  int lastLogIndex = getLastLogIndex();
  myAssert(logIndex <= lastLogIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > "
                  "lastLogIndex{%d}",
                  m_me, logIndex, lastLogIndex));
  int SliceIndex = logIndex - m_lastSnapshotIncludeIndex - 1;
  return SliceIndex;
}

bool Raft::sendRequestVote(
    int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
    std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply,
    std::shared_ptr<int> votedNum) {

  auto start = now();
  DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 發送 RequestVote 開始",
          m_me, m_currentTerm, getLastLogIndex());
  bool ok = m_peers[server]->RequestVote(args.get(), reply.get());
  DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 發送 RequestVote "
          "完畢，耗時:{%d} ms",
          m_me, m_currentTerm, getLastLogIndex(), now() - start);

  if (!ok) {
    return ok;
  }

  std::lock_guard<std::mutex> lg(m_mtx);
  if (reply->term() > m_currentTerm) {
    m_status = Follower;
    m_currentTerm = reply->term();
    m_votedFor = -1;
    persist();
    return true;
  } else if (reply->term() < m_currentTerm) {
    return true;
  }
  myAssert(reply->term() == m_currentTerm,
           format("assert {reply.Term==rf.currentTerm} fail"));

  if (!reply->votegranted()) {
    return true;
  }

  *votedNum = *votedNum + 1;
  if (*votedNum >= m_peers.size() / 2 + 1) {

    *votedNum = 0;
    if (m_status == Leader) {

      myAssert(false, format("[func-sendRequestVote-rf{%d}]  term:{%d} "
                             "同一个term当两次领导，error",
                             m_me, m_currentTerm));
    }

    m_status = Leader;

    DPrintf("[func-sendRequestVote rf{%d}] elect success  ,current term:{%d} "
            ",lastLogIndex:{%d}\n",
            m_me, m_currentTerm, getLastLogIndex());

    int lastLogIndex = getLastLogIndex();
    for (int i = 0; i < m_nextIndex.size(); i++) {
      m_nextIndex[i] = lastLogIndex + 1;
      m_matchIndex[i] = 0;
    }
    std::thread t(&Raft::doHeartBeat, this);
    t.detach();

    persist();
  }
  return true;
}

bool Raft::sendAppendEntries(
    int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
    std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
    std::shared_ptr<int> appendNums) {

  DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE "
          "rpc開始 ， args->entries_size():{%d}",
          m_me, server, args->entries_size());
  bool ok = m_peers[server]->AppendEntries(args.get(), reply.get());

  if (!ok) {
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE "
            "rpc失敗",
            m_me, server);
    return ok;
  }
  DPrintf(
      "[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功",
      m_me, server);
  if (reply->appstate() == Disconnected) {
    return ok;
  }
  std::lock_guard<std::mutex> lg1(m_mtx);

  if (reply->term() > m_currentTerm) {
    m_status = Follower;
    m_currentTerm = reply->term();
    m_votedFor = -1;
    return ok;
  } else if (reply->term() < m_currentTerm) {
    DPrintf("[func -sendAppendEntries  rf{%d}]  "
            "节点：{%d}的term{%d}<rf{%d}的term{%d}\n",
            m_me, server, reply->term(), m_me, m_currentTerm);
    return ok;
  }

  if (m_status != Leader) {

    return ok;
  }

  myAssert(reply->term() == m_currentTerm,
           format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(),
                  m_currentTerm));
  if (!reply->success()) {

    if (reply->updatenextindex() != -100) {

      DPrintf("[func -sendAppendEntries  rf{%d}]  "
              "返回的日志term相等，但是不匹配，回缩nextIndex[%d]：{%d}\n",
              m_me, server, reply->updatenextindex());
      m_nextIndex[server] = reply->updatenextindex();
    }

  } else {
    *appendNums = *appendNums + 1;
    DPrintf("---------------------------tmp------------------------- "
            "節點{%d}返回true,當前*appendNums{%d}",
            server, *appendNums);

    m_matchIndex[server] = std::max(
        m_matchIndex[server], args->prevlogindex() + args->entries_size());
    m_nextIndex[server] = m_matchIndex[server] + 1;
    int lastLogIndex = getLastLogIndex();

    myAssert(m_nextIndex[server] <= lastLogIndex + 1,
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) "
                    "= %d   lastLogIndex{%d} = %d",
                    server, m_logs.size(), server, lastLogIndex));
    if (*appendNums >= 1 + m_peers.size() / 2) {

      *appendNums = 0;

      if (args->entries_size() > 0) {
        DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   "
                "m_currentTerm{%d}",
                args->entries(args->entries_size() - 1).logterm(),
                m_currentTerm);
      }
      if (args->entries_size() > 0 &&
          args->entries(args->entries_size() - 1).logterm() == m_currentTerm) {
        DPrintf("---------------------------tmp------------------------- "
                "當前term有log成功提交，更新leader的m_commitIndex "
                "from{%d} to{%d}",
                m_commitIndex, args->prevlogindex() + args->entries_size());

        m_commitIndex = std::max(m_commitIndex,
                                 args->prevlogindex() + args->entries_size());
      }
      myAssert(m_commitIndex <= lastLogIndex,
               format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  "
                      "rf.commitIndex:%d\n",
                      m_me, lastLogIndex, m_commitIndex));
    }
  }
  return ok;
}

void Raft::AppendEntries(google::protobuf::RpcController *controller,
                         const ::raftRpcProctoc::AppendEntriesArgs *request,
                         ::raftRpcProctoc::AppendEntriesReply *response,
                         ::google::protobuf::Closure *done) {
  AppendEntries1(request, response);
  done->Run();
}

void Raft::InstallSnapshot(
    google::protobuf::RpcController *controller,
    const ::raftRpcProctoc::InstallSnapshotRequest *request,
    ::raftRpcProctoc::InstallSnapshotResponse *response,
    ::google::protobuf::Closure *done) {
  InstallSnapshot(request, response);

  done->Run();
}

void Raft::RequestVote(google::protobuf::RpcController *controller,
                       const ::raftRpcProctoc::RequestVoteArgs *request,
                       ::raftRpcProctoc::RequestVoteReply *response,
                       ::google::protobuf::Closure *done) {
  RequestVote(request, response);
  done->Run();
}

void Raft::Start(Op command, int *newLogIndex, int *newLogTerm,
                 bool *isLeader) {
  std::lock_guard<std::mutex> lg1(m_mtx);

  if (m_status != Leader) {
    DPrintf("[func-Start-rf{%d}]  is not leader");
    *newLogIndex = -1;
    *newLogTerm = -1;
    *isLeader = false;
    return;
  }

  raftRpcProctoc::LogEntry newLogEntry;
  newLogEntry.set_command(command.asString());
  newLogEntry.set_logterm(m_currentTerm);
  newLogEntry.set_logindex(getNewCommandIndex());
  m_logs.emplace_back(newLogEntry);

  int lastLogIndex = getLastLogIndex();

  DPrintf("[func-Start-rf{%d}]  lastLogIndex:%d,command:%s\n", m_me,
          lastLogIndex, &command);

  persist();
  *newLogIndex = newLogEntry.logindex();
  *newLogTerm = newLogEntry.logterm();
  *isLeader = true;
}

void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me,
                std::shared_ptr<Persister> persister,
                std::shared_ptr<LockQueue<ApplyMsg>> applyCh) {
  m_peers = peers;
  m_persister = persister;
  m_me = me;

  m_mtx.lock();

  this->applyChan = applyCh;

  m_currentTerm = 0;
  m_status = Follower;
  m_commitIndex = 0;
  m_lastApplied = 0;
  m_logs.clear();
  for (int i = 0; i < m_peers.size(); i++) {
    m_matchIndex.push_back(0);
    m_nextIndex.push_back(0);
  }
  m_votedFor = -1;

  m_lastSnapshotIncludeIndex = 0;
  m_lastSnapshotIncludeTerm = 0;
  m_lastResetElectionTime = now();
  m_lastResetHearBeatTime = now();

  readPersist(m_persister->ReadRaftState());
  if (m_lastSnapshotIncludeIndex > 0) {
    m_lastApplied = m_lastSnapshotIncludeIndex;
  }

  DPrintf("[Init&ReInit] Sever %d, term %d, lastSnapshotIncludeIndex {%d} , "
          "lastSnapshotIncludeTerm {%d}",
          m_me, m_currentTerm, m_lastSnapshotIncludeIndex,
          m_lastSnapshotIncludeTerm);

  m_mtx.unlock();

  m_ioManager = std::make_unique<monsoon::IOManager>(FIBER_THREAD_NUM,
                                                     FIBER_USE_CALLER_THREAD);

  m_ioManager->scheduler([this]() -> void { this->leaderHearBeatTicker(); });
  m_ioManager->scheduler([this]() -> void { this->electionTimeOutTicker(); });

  std::thread t3(&Raft::applierTicker, this);
  t3.detach();
}

std::string Raft::persistData() {
  BoostPersistRaftNode boostPersistRaftNode;
  boostPersistRaftNode.m_currentTerm = m_currentTerm;
  boostPersistRaftNode.m_votedFor = m_votedFor;
  boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;
  boostPersistRaftNode.m_lastSnapshotIncludeTerm = m_lastSnapshotIncludeTerm;
  for (auto &item : m_logs) {
    boostPersistRaftNode.m_logs.push_back(item.SerializeAsString());
  }

  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);
  oa << boostPersistRaftNode;
  return ss.str();
}

void Raft::readPersist(std::string data) {
  if (data.empty()) {
    return;
  }
  std::stringstream iss(data);
  boost::archive::text_iarchive ia(iss);

  BoostPersistRaftNode boostPersistRaftNode;
  ia >> boostPersistRaftNode;

  m_currentTerm = boostPersistRaftNode.m_currentTerm;
  m_votedFor = boostPersistRaftNode.m_votedFor;
  m_lastSnapshotIncludeIndex = boostPersistRaftNode.m_lastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = boostPersistRaftNode.m_lastSnapshotIncludeTerm;
  m_logs.clear();
  for (auto &item : boostPersistRaftNode.m_logs) {
    raftRpcProctoc::LogEntry logEntry;
    logEntry.ParseFromString(item);
    m_logs.emplace_back(logEntry);
  }
}

void Raft::Snapshot(int index, std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);

  if (m_lastSnapshotIncludeIndex >= index || index > m_commitIndex) {
    DPrintf("[func-Snapshot-rf{%d}] rejects replacing log with snapshotIndex "
            "%d as current snapshotIndex %d is larger or "
            "smaller ",
            m_me, index, m_lastSnapshotIncludeIndex);
    return;
  }
  auto lastLogIndex = getLastLogIndex();

  int newLastSnapshotIncludeIndex = index;
  int newLastSnapshotIncludeTerm =
      m_logs[getSlicesIndexFromLogIndex(index)].logterm();
  std::vector<raftRpcProctoc::LogEntry> trunckedLogs;

  for (int i = index + 1; i <= getLastLogIndex(); i++) {

    trunckedLogs.push_back(m_logs[getSlicesIndexFromLogIndex(i)]);
  }
  m_lastSnapshotIncludeIndex = newLastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = newLastSnapshotIncludeTerm;
  m_logs = trunckedLogs;
  m_commitIndex = std::max(m_commitIndex, index);
  m_lastApplied = std::max(m_lastApplied, index);

  m_persister->Save(persistData(), snapshot);

  DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen "
          "{%d}",
          m_me, index, m_lastSnapshotIncludeTerm, m_logs.size());
  myAssert(m_logs.size() + m_lastSnapshotIncludeIndex == lastLogIndex,
           format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != "
                  "lastLogjInde{%d}",
                  m_logs.size(), m_lastSnapshotIncludeIndex, lastLogIndex));
}