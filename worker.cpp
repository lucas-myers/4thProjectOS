#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

using namespace std;

struct Message {
    long mtype;
    int index;
    int quantum;
};

int main() {
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

    // Day 1 

    //will receive dispatch messages and respond to oss

    cout << "Worker placeholder started. PID: " << getpid() << endl;

    return 0;
}