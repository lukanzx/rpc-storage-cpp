#ifndef CONFIG_H
#define CONFIG_H

const bool Debug = true;

const int debugMul = 1;
const int HeartBeatTimeout = 25 * debugMul;
const int ApplyInterval = 10 * debugMul;

const int minRandomizedElectionTime = 300 * debugMul;
const int maxRandomizedElectionTime = 500 * debugMul;

const int CONSENSUS_TIMEOUT = 500 * debugMul;

const int FIBER_THREAD_NUM = 1;
const bool FIBER_USE_CALLER_THREAD = false;

#endif
