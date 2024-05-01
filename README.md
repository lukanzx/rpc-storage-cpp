# RaftStorage
The purpose of this project is to learn the principles of Raft and implement a simple k-v storage database. <br>
Therefore, it is not suitable for production environments. <br>

## usage method

### Third party library requirements
- muduo
- boost
- protoc

### Compile Start
#### Using RPC

```
mkdir cmake-build-debug
cd cmake-build-debug
cmake ..
make
```

- consumer
- provider
- Just run it, pay attention to running the provider first, then the consumer. 
- The reason is simple: you need to provide the RPC service first before calling it.

#### Using Raft Cluster
```
mkdir cmake-build-debug
cd cmake-build-debug
cmake..
make
```

```
raftCoreRun -n 3 -f test.conf
```

#### Using KV
After starting the raft cluster, simply start `callerMain`.


