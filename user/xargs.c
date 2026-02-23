#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
    int pid, status;
    //char input[argc][MAXARG];
    char buf[100];
    
    pid = fork();

    if(pid == 0){   //child
        int i = 0;
        for(i; i < argc; i++){
            int length = read(0, buf, 1);
            //input[i] = buf;
            if(strcmp(argv[i], "\n") == 0 || length == 0){
                break;
            }
        }
        exec(buf);
    } 
    
    else {        //parent
        wait(&status);
        int i = 0;
        for(i; i <= argc; i++){
            int length = read(0, buf, 1);
            if(strcmp(argv[i], "\n") == 0 || length == 0){
                break;
            }
        }
        
        exec(buf);
    }

    exit(0);
}