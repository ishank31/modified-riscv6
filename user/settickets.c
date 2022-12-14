#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char *argv[])
{
    if(argc != 1)
    {
        fprintf(2, "Invalid number of arguments\n");
        exit(1);
    }

    settickets(atoi(argv[1]));
    exit(1);
}