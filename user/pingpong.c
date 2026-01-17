#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main()
{
   int pid;
   char child,parent;
   int tochild[2];
   int toparent[2];
   char buf[100];

   pipe(tochild);
   pipe(toparent);
   pid = fork();

   if(pid == 0){   //child

      int numread = read(tochild[0], buf, sizeof(buf));

      child = getpid();
      printf("%d: received ping\n", child);

      write(toparent[1], buf, numread);

   }else{   //parent

      write(tochild[1], "1", 1);
      read(toparent[0], buf, sizeof(buf));

      parent = getpid();
      printf("%d: received pong\n", parent);
   }

   close(tochild[1]);
   close(tochild[0]);
   close(toparent[1]);
   close(toparent[0]);

   exit(0);
}

