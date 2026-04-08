#include <iostream>
#include <fstream>
#include <queue>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>

using namespace std;

// Constants for limits and timing
const int MAX_TOTAL_PROCESSES = 20;
const int PCB_SIZE = 18;
const unsigned int BILLION = 1000000000;
const unsigned int BASE_QUANTUM = 25000000;   // 25ms time slice
const unsigned int BLOCK_TIME = 100000000;    // time a process stays blocked
const unsigned int IDLE_INCREMENT = 100000;   // how much time advances when idle
const unsigned int DISPATCH_OVERHEAD = 1000;  // cost of dispatching a process
const unsigned int UNBLOCK_OVERHEAD = 5000;   // cost of unblocking
const int MAX_LOG_LINES = 10000;

// Simulated clock shared between OSS and workers
struct SimClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

// PCB (process control block) for each process
struct PCB {
    int occupied;              // if slot is being used
    pid_t pid;                 // actual system PID
    int localPid;              // index in table (P0, P1, etc.)
    unsigned int startSeconds;
    unsigned int startNano;
    unsigned int serviceTimeSeconds;
    unsigned int serviceTimeNano;
    unsigned int eventWaitSec; // when process will unblock
    unsigned int eventWaitNano;
    int blocked;               // 1 = blocked, 0 = ready
};

// Message struct for message queue communication
struct Message {
    long mtype;
    int index;
    int quantum;
};

int shmId = -1;
int msgId = -1;
SimClock* simClock = nullptr;

PCB processTable[PCB_SIZE];
queue<int> readyQueue; // Round Robin queue

// Simulation settings
int totalChildren = 5;
int maxSimultaneous = 2;
double timeLimitForChildren = 3.0;
double launchInterval = 0.5;
string logFileName = "oss.log";

ofstream logFile;
int logLines = 0;

// Tracking stats
int launchedTotal = 0;
int finishedTotal = 0;
int runningNow = 0;

// Time tracking for events
unsigned int nextLaunchSec = 0;
unsigned int nextLaunchNano = 0;
unsigned int nextTablePrintSec = 0;
unsigned int nextTablePrintNano = 500000000;

// Performance stats
unsigned long long cpuBusyTime = 0;
unsigned long long systemOverheadTime = 0;
unsigned long long idleTime = 0;

// Writes to log file (stops after limit)
void writeLog(const string& text) {
    if (logLines < MAX_LOG_LINES) {
        logFile << text;
        logLines++;
        if (logLines == MAX_LOG_LINES) {
            logFile << "OSS: Log limit reached, suppressing further log output.\n";
        }
    }
}

// Adds nanoseconds and handles rollover into seconds
void addToTime(unsigned int& sec, unsigned int& nano, unsigned int addNano) {
    nano += addNano;
    while (nano >= BILLION) {
        nano -= BILLION;
        sec++;
    }
}

// Advances the simulated clock
void advanceClock(unsigned int ns) {
    addToTime(simClock->seconds, simClock->nanoseconds, ns);
}

// Checks if current time reached a target time
bool timeReached(unsigned int sec1, unsigned int nano1,
                 unsigned int sec2, unsigned int nano2) {
    if (sec1 > sec2) return true;
    if (sec1 == sec2 && nano1 >= nano2) return true;
    return false;
}

// Clean up shared memory and message queue
void cleanup() {
    if (simClock != nullptr) {
        shmdt(simClock);
        simClock = nullptr;
    }

    if (shmId != -1) {
        shmctl(shmId, IPC_RMID, nullptr);
        shmId = -1;
    }

    if (msgId != -1) {
        msgctl(msgId, IPC_RMID, nullptr);
        msgId = -1;
    }
}

// Kill all running child processes
void killChildren() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].pid > 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }

    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

// Handles CTRL+C or timeout
void signalHandler(int sig) {
    cerr << "\nOSS: caught signal " << sig << ", cleaning up.\n";
    killChildren();
    cleanup();
    exit(1);
}

// Parses command line arguments
void parseArguments(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'n': totalChildren = atoi(optarg); break;
            case 's': maxSimultaneous = atoi(optarg); break;
            case 't': timeLimitForChildren = atof(optarg); break;
            case 'i': launchInterval = atof(optarg); break;
            case 'f': logFileName = optarg; break;
        }
    }
}

// Initializes process table (all empty)
void initProcessTable() {
    for (int i = 0; i < PCB_SIZE; i++) {
        processTable[i].occupied = 0;
        processTable[i].blocked = 0;
    }
}

// Finds a free slot in PCB
int getFreePCB() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (!processTable[i].occupied) return i;
    }
    return -1;
}

// Launches a new worker process
void launchWorker(int index) {
    pid_t pid = fork();

    if (pid == 0) {
        // child runs worker program
        execl("./worker", "worker",
              to_string(timeLimitForChildren).c_str(),
              to_string(index).c_str(),
              (char*)nullptr);
        exit(1);
    }

    // parent sets up PCB
    processTable[index].occupied = 1;
    processTable[index].pid = pid;
    processTable[index].localPid = index;

    readyQueue.push(index); // add to ready queue

    writeLog("OSS: Generating process and putting in ready queue\n");
}

// Checks if blocked processes are ready again
void checkBlockedProcesses() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].blocked) {

            // if time passed  unblock it
            if (timeReached(simClock->seconds, simClock->nanoseconds,
                            processTable[i].eventWaitSec,
                            processTable[i].eventWaitNano)) {

                processTable[i].blocked = 0;
                readyQueue.push(i);

                writeLog("OSS: Unblocking process and putting back in ready queue\n");

                advanceClock(UNBLOCK_OVERHEAD);
                systemOverheadTime += UNBLOCK_OVERHEAD;
            }
        }
    }
}