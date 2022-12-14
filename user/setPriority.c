#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char *argv[])
{
    if(argc < 3 || argc > 3)
    {
        fprintf(2, "Enter valid number of arguments\n", argv[0]);
        exit(1);
    }

    int pid = atoi(argv[2]);
    int priority = atoi(argv[1]);

    if(priority<0 || priority>100)
    {
        fprintf(2, "Priority must be between 0 and 100\n");
        exit(1);
    }
    setPriority(priority,pid);
    exit(1);
}