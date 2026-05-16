#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
void handler(int s) { exit(1); }
int main() { signal(SIGUSR1, handler); pause(); }
