#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

using namespace std;

const long long BILLION_LL = 1000000000LL;

struct Message {
    long mtype;
    int index;
    int quantum;
};

long long randomLongLong(long long maxValue) {
    if (maxValue <= 1) {
        return 1;
    }

    long long high = static_cast<long long>(rand());
    long long low = static_cast<long long>(rand());
    long long combined = (high << 31) ^ low;

    if (combined < 0) {
        combined = -combined;
    }

    return (combined % maxValue) + 1;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "worker: missing arguments\n";
        return 1;
    }

    double burstLimitSeconds = atof(argv[1]);
    int localIndex = atoi(argv[2]);

    long long maxBurstNano = static_cast<long long>(burstLimitSeconds * BILLION_LL);
    if (maxBurstNano <= 0) {
        maxBurstNano = BILLION_LL;
    }

    srand(static_cast<unsigned int>(getpid() ^ time(nullptr) ^ (localIndex * 7919)));

    // Each process gets a random total CPU burst from 1 ns up to the -t limit.
    // This fixes the old integer overflow where 3 seconds became negative.
    long long totalBurstNano = randomLongLong(maxBurstNano);
    long long usedSoFar = 0;

    key_t msgKey = ftok(".", 75);
    if (msgKey == -1) {
        perror("worker ftok");
        return 1;
    }

    int msgId = msgget(msgKey, 0666);
    if (msgId == -1) {
        perror("worker msgget");
        return 1;
    }

    while (true) {
        Message msg;

        if (msgrcv(msgId, &msg, sizeof(Message) - sizeof(long), getpid(), 0) == -1) {
            perror("worker msgrcv");
            return 1;
        }

        int quantum = msg.quantum;
        long long remaining = totalBurstNano - usedSoFar;

        Message reply;
        reply.mtype = 1;
        reply.index = localIndex;
        reply.quantum = 0;

        if (remaining <= quantum) {
            // Negative means the process terminated after using this much time.
            usedSoFar += remaining;
            reply.quantum = -static_cast<int>(remaining);

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd terminate");
                return 1;
            }

            break;
        }

        int blockChance = rand() % 100;

        if (blockChance < 20) {
            // Positive and less than the quantum means I/O interrupt / blocked.
            int used = (rand() % (quantum - 1)) + 1;

            if (used > remaining) {
                used = static_cast<int>(remaining);
            }

            usedSoFar += used;
            reply.quantum = used;

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd blocked");
                return 1;
            }
        } else {
            // Equal to the quantum means it used the whole time slice.
            usedSoFar += quantum;
            reply.quantum = quantum;

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd full");
                return 1;
            }
        }
    }

    return 0;
}
