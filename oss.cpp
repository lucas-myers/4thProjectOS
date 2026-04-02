#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
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
const unsigned int BASE_QUANTUM = 25000000; // 25ms

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

int totalChildren = 5;
int maxSimultaneous = 2;
double timeLimitForChildren = 2.0;
double launchInterval = 0.5;
string logFileName = "oss.log";

ofstream logFile;

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

void signalHandler(int sig) {
    cerr << "\nOSS: Caught signal " << sig << ", cleaning up and terminating.\n";
    cleanup();

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }

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
    if (maxSimultaneous < 1) maxSimultaneous = 1;
    if (maxSimultaneous > MAX_PROCESSES) maxSimultaneous = MAX_PROCESSES;
    if (totalChildren > MAX_PROCESSES) totalChildren = MAX_PROCESSES;
    if (timeLimitForChildren < 0) timeLimitForChildren = 1.0;
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

void printProcessTable() {
    logFile << "\nOSS: Process Table at time "
            << simClock->seconds << ":" << simClock->nanoseconds << "\n";
    logFile << left
            << setw(8)  << "Entry"
            << setw(10) << "Occ"
            << setw(10) << "PID"
            << setw(10) << "Local"
            << setw(14) << "StartS"
            << setw(14) << "StartN"
            << setw(14) << "ServS"
            << setw(14) << "ServN"
            << setw(14) << "Block"
            << "\n";

    for (int i = 0; i < MAX_PROCESSES; i++) {
        logFile << left
                << setw(8)  << i
                << setw(10) << processTable[i].occupied
                << setw(10) << processTable[i].pid
                << setw(10) << processTable[i].localPid
                << setw(14) << processTable[i].startSeconds
                << setw(14) << processTable[i].startNano
                << setw(14) << processTable[i].serviceTimeSeconds
                << setw(14) << processTable[i].serviceTimeNano
                << setw(14) << processTable[i].blocked
                << "\n";
    }
    logFile << endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGALRM, signalHandler);

    alarm(20);

    parseArguments(argc, argv);

    logFile.open(logFileName.c_str(), ios::out | ios::trunc);
    if (!logFile) {
        cerr << "Failed to open log file.\n";
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

    logFile << "OSS: Starting simulator\n";
    logFile << "OSS: totalChildren = " << totalChildren << "\n";
    logFile << "OSS: maxSimultaneous = " << maxSimultaneous << "\n";
    logFile << "OSS: timeLimitForChildren = " << timeLimitForChildren << "\n";
    logFile << "OSS: launchInterval = " << launchInterval << "\n";
    logFile << "OSS: base quantum = " << BASE_QUANTUM << " ns\n";

    for (int i = 0; i < 10; i++) {
        advanceClock(1000000); // 1ms fake setup work
    }

    printProcessTable();

    logFile << "OSS: Day 1 \n";

    cleanup();
    logFile.close();
    return 0;
}