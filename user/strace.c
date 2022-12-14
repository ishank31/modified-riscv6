#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) 
{
    
    int max_commands=32;
    char *new_argv[max_commands];

    // if(argc < 3 || argv[1][0] < '0' || argv[1][0] > '9')
    // {
    //     fprintf(2, "Usage: %s mask command\n", argv[0]);
    //     exit(1);
    // }

    if(argc < 3 || argc > max_commands)
    {
        fprintf(2, "Enter valid range of arguments");
        exit(1);
    }
    else{
        int check=trace(atoi(argv[1]));
        if (check < 0) 
        {
            fprintf(2, "%s: strace  command failed\n", argv[0]);
            exit(1);
        }
        else{
            for(int i = 2; i < argc && i < max_commands; i++)
    	    new_argv[i-2] = argv[i];

            exec(new_argv[0], new_argv);
            exit(0);
        }
    }
    
}
