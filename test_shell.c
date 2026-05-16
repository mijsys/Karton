#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t flag = 0;
static void handle(int signo) { flag = 1; }

int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    printf("Ready. PID: %d\n", getpid());
    while (!flag) {
        pause();
    }
    printf("Done.\n");
    return 0;
}
