#include <signal.h>
#include <stdio.h>
#include <string.h>

int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    int rc = sigaction(SIGUSR1, &sa, NULL);
    printf("rc=%d\n", rc);
    return 0;
}
