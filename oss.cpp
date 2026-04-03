#include <iostream>
#include <iomanip>
#include <fstream>
#include <queue>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

const int MAX_PROCESSES = 20;
const unsigned int BILLION = 1000000000;
const unsigned int BASE_QUANTUM = 25000000; // 25 ms

struct SimClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

struct PCB {
    int occupied;
    pid_t pid;
    int localPid;
    unsigned int startSeconds;
    unsigned int startNano;
    unsigned int serviceTimeSeconds;
    unsigned int serviceTimeNano;
    unsigned int eventWaitSec;
    unsigned int eventWaitNano;
    int blocked;
};

struct Message {
    long mtype;
    int index;
    int quantum;
};

int shmId = -1;
int msgId = -1;
SimClock* simClock = nullptr;

PCB processTable[MAX_PROCESSES];
queue<int> readyQueue;

int totalChildren = 5;
int maxSimultaneous = 2;
double timeLimitForChildren = 3.0;
double launchInterval = 0.5;
string logFileName = "oss.log";

ofstream logFile;

int running = 0;
int totalLaunched = 0;
int totalFinished = 0;

unsigned int nextLaunchSec = 0;
unsigned int nextLaunchNano = 0;

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

void killChildren() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].occupied && processTable[i].pid > 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

void signalHandler(int sig) {
    cerr << "\nOSS: Caught signal " << sig << ". Cleaning up.\n";
    killChildren();
    cleanup();
    exit(1);
}

void printUsage(const char* progName) {
    cout << "Usage: " << progName
         << " [-h] [-n proc] [-s simul] [-t timelimitForChildren] "
         << "[-i fractionOfSecondToLaunchChildren] [-f logfile]\n";
}

void parseArguments(int argc, char* argv[]) {
    int opt;

    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'n':
                totalChildren = atoi(optarg);
                break;
            case 's':
                maxSimultaneous = atoi(optarg);
                break;
            case 't':
                timeLimitForChildren = atof(optarg);
                break;
            case 'i':
                launchInterval = atof(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                printUsage(argv[0]);
                exit(1);
        }
    }

    if (totalChildren < 1) totalChildren = 1;
    if (totalChildren > MAX_PROCESSES) totalChildren = MAX_PROCESSES;

    if (maxSimultaneous < 1) maxSimultaneous = 1;
    if (maxSimultaneous > MAX_PROCESSES) maxSimultaneous = MAX_PROCESSES;

    if (timeLimitForChildren <= 0) timeLimitForChildren = 1.0;
    if (launchInterval < 0) launchInterval = 0.0;
}

void initProcessTable() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].localPid = i;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
        processTable[i].serviceTimeSeconds = 0;
        processTable[i].serviceTimeNano = 0;
        processTable[i].eventWaitSec = 0;
        processTable[i].eventWaitNano = 0;
        processTable[i].blocked = 0;
    }
}

void advanceClock(unsigned int ns) {
    simClock->nanoseconds += ns;
    while (simClock->nanoseconds >= BILLION) {
        simClock->seconds++;
        simClock->nanoseconds -= BILLION;
    }
}

void addToTime(unsigned int &sec, unsigned int &nano, unsigned int addNano) {
    nano += addNano;
    while (nano >= BILLION) {
        sec++;
        nano -= BILLION;
    }
}

void addServiceTime(int index, unsigned int addNano) {
    addToTime(processTable[index].serviceTimeSeconds,
              processTable[index].serviceTimeNano,
              addNano);
}

int getFreePCB() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processTable[i].occupied) {
            return i;
        }
    }
    return -1;
}

bool timeToLaunch() {
    if (simClock->seconds > nextLaunchSec) return true;
    if (simClock->seconds == nextLaunchSec &&
        simClock->nanoseconds >= nextLaunchNano) return true;
    return false;
}

void setNextLaunchTime(double interval) {
    unsigned int ns = static_cast<unsigned int>(interval * BILLION);
    nextLaunchSec = simClock->seconds;
    nextLaunchNano = simClock->nanoseconds;
    addToTime(nextLaunchSec, nextLaunchNano, ns);
}

void printReadyQueue() {
    queue<int> temp = readyQueue;

    logFile << "OSS: Ready queue [ ";
    while (!temp.empty()) {
        int idx = temp.front();
        temp.pop();
        logFile << "P" << idx << " ";
    }
    logFile << "]\n";
}

void printProcessTable() {
    logFile << "\nOSS: Process Table at time "
            << simClock->seconds << ":" << simClock->nanoseconds << "\n";

    logFile << left
            << setw(6)  << "Entry"
            << setw(10) << "Occupied"
            << setw(10) << "PID"
            << setw(10) << "Local"
            << setw(12) << "StartS"
            << setw(12) << "StartN"
            << setw(12) << "ServS"
            << setw(12) << "ServN"
            << setw(10) << "Blocked"
            << "\n";

    for (int i = 0; i < MAX_PROCESSES; i++) {
        logFile << left
                << setw(6)  << i
                << setw(10) << processTable[i].occupied
                << setw(10) << processTable[i].pid
                << setw(10) << processTable[i].localPid
                << setw(12) << processTable[i].startSeconds
                << setw(12) << processTable[i].startNano
                << setw(12) << processTable[i].serviceTimeSeconds
                << setw(12) << processTable[i].serviceTimeNano
                << setw(10) << processTable[i].blocked
                << "\n";
    }
    logFile << "\n";
}

void removeFromPCB(int index) {
    processTable[index].occupied = 0;
    processTable[index].pid = 0;
    processTable[index].startSeconds = 0;
    processTable[index].startNano = 0;
    processTable[index].serviceTimeSeconds = 0;
    processTable[index].serviceTimeNano = 0;
    processTable[index].eventWaitSec = 0;
    processTable[index].eventWaitNano = 0;
    processTable[index].blocked = 0;
}

void launchWorker(int index) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        string burstStr = to_string(timeLimitForChildren);
        string indexStr = to_string(index);

        execl("./worker", "worker", burstStr.c_str(), indexStr.c_str(), (char*)nullptr);
        perror("execl");
        exit(1);
    }

    processTable[index].occupied = 1;
    processTable[index].pid = pid;
    processTable[index].localPid = index;
    processTable[index].startSeconds = simClock->seconds;
    processTable[index].startNano = simClock->nanoseconds;
    processTable[index].serviceTimeSeconds = 0;
    processTable[index].serviceTimeNano = 0;
    processTable[index].blocked = 0;

    readyQueue.push(index);

    logFile << "OSS: Generating process with PID " << pid
            << " and putting it in ready queue at time "
            << simClock->seconds << ":" << simClock->nanoseconds << "\n";
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(30);

    parseArguments(argc, argv);

    logFile.open(logFileName.c_str(), ios::out | ios::trunc);
    if (!logFile) {
        cerr << "Error opening log file.\n";
        return 1;
    }

    key_t shmKey = ftok(".", 65);
    if (shmKey == -1) {
        perror("ftok shm");
        return 1;
    }

    shmId = shmget(shmKey, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmId == -1) {
        perror("shmget");
        return 1;
    }

    simClock = (SimClock*)shmat(shmId, nullptr, 0);
    if (simClock == (void*)-1) {
        perror("shmat");
        simClock = nullptr;
        cleanup();
        return 1;
    }

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    key_t msgKey = ftok(".", 75);
    if (msgKey == -1) {
        perror("ftok msg");
        cleanup();
        return 1;
    }

    msgId = msgget(msgKey, IPC_CREAT | 0666);
    if (msgId == -1) {
        perror("msgget");
        cleanup();
        return 1;
    }

    initProcessTable();
    setNextLaunchTime(0.0);

    unsigned int nextTablePrintSec = 0;
    unsigned int nextTablePrintNano = 500000000;

    while (totalFinished < totalChildren) {
        while (running < maxSimultaneous &&
               totalLaunched < totalChildren &&
               timeToLaunch()) {

            int index = getFreePCB();
            if (index == -1) {
                break;
            }

            launchWorker(index);
            running++;
            totalLaunched++;

            setNextLaunchTime(launchInterval);
            advanceClock(1000); // scheduling/launch overhead
        }

        if (!readyQueue.empty()) {
            printReadyQueue();

            int index = readyQueue.front();
            readyQueue.pop();

            pid_t childPid = processTable[index].pid;

            logFile << "OSS: Dispatching process with PID " << childPid
                    << " from ready queue at time "
                    << simClock->seconds << ":" << simClock->nanoseconds << "\n";

            advanceClock(1000); // dispatch overhead
            logFile << "OSS: total time this dispatch was 1000 nanoseconds\n";

            Message dispatchMsg;
            dispatchMsg.mtype = childPid;
            dispatchMsg.index = index;
            dispatchMsg.quantum = BASE_QUANTUM;

            if (msgsnd(msgId, &dispatchMsg, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("msgsnd dispatch");
                killChildren();
                cleanup();
                return 1;
            }

            Message replyMsg;
            if (msgrcv(msgId, &replyMsg, sizeof(Message) - sizeof(long), 1, 0) == -1) {
                perror("msgrcv reply");
                killChildren();
                cleanup();
                return 1;
            }

            int usedTime = replyMsg.quantum;

            if (usedTime < 0) {
                usedTime = -usedTime;

                logFile << "OSS: Receiving that process with PID " << childPid
                        << " terminated after running for "
                        << usedTime << " nanoseconds\n";

                advanceClock(static_cast<unsigned int>(usedTime));
                addServiceTime(index, static_cast<unsigned int>(usedTime));

                waitpid(childPid, nullptr, 0);

                removeFromPCB(index);
                running--;
                totalFinished++;

                logFile << "OSS: Process with PID " << childPid
                        << " removed from system\n";
            } else {
                logFile << "OSS: Receiving that process with PID " << childPid
                        << " ran for " << usedTime << " nanoseconds\n";

                advanceClock(static_cast<unsigned int>(usedTime));
                addServiceTime(index, static_cast<unsigned int>(usedTime));

                readyQueue.push(index);

                logFile << "OSS: Putting process with PID " << childPid
                        << " into ready queue\n";
            }
        } else {
            advanceClock(100000); // no ready process, simulated idle time
        }

        if (simClock->seconds > nextTablePrintSec ||
            (simClock->seconds == nextTablePrintSec &&
             simClock->nanoseconds >= nextTablePrintNano)) {

            printProcessTable();
            addToTime(nextTablePrintSec, nextTablePrintNano, 500000000);
        }
    }

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }

    logFile << "\nOSS: Total processes launched: " << totalLaunched << "\n";
    logFile << "OSS: Total processes finished: " << totalFinished << "\n";
    logFile << "OSS: Simulation finished at time "
            << simClock->seconds << ":" << simClock->nanoseconds << "\n";

    cleanup();
    logFile.close();
    return 0;
}