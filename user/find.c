#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  //returns path as string?
  for(p=path+strlen(path); p >= path; p--);
  p++;

  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
find(char *path, char *file)

{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){ //open directory/file
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){ //metadata of file opened
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){ //states type of thing opened
  case T_DEVICE:
  case T_FILE:
    printf("%s %d %d %d\n", fmtname(path), st.type, st.ino, (int) st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){ //read through directory
      if(de.inum == 0)
        continue; //skip directory entry not in use
      memmove(p, de.name, DIRSIZ); //if in use copy name, add to path
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      if(strcmp(file, de.name) == 0){
        printf("%s\n", buf);
      }

        if(st.type == T_DIR){
          if((strcmp(de.name, ".") != 0) || (strcmp(de.name, "..") != 0)){
            printf("buf: %s\n",  buf);
            find(fmtname(buf), file);
          }
        }
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    printf("usage: find <path> <file>\n");
    exit(0);
  }

  find(argv[1], argv[2]);
  exit(0);
}
