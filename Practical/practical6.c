#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void handler(int sig)
{
    if (sig == SIGINT)
        printf("\nSIGINT Received\n");

    if (sig == SIGTERM)
        printf("\nSIGTERM Received\n");

    if (sig == SIGUSR1)
        printf("\nSIGUSR1 Received\n");
}

int main()
{
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGUSR1, handler);

    while (1)
    {
        printf("Running ... \n");
        sleep(3);
    }

    return 0;
}
