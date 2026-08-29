#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
struct ImDir { HANDLE handle; WIN32_FIND_DATAA data; int first; char pattern[1024]; };
ImDir *im_dir_open(const char *path) { ImDir *d=(ImDir*)calloc(1,sizeof *d); if(!d)return NULL; snprintf(d->pattern,sizeof d->pattern,"%s\\*",path?path:"."); d->handle=FindFirstFileA(d->pattern,&d->data); if(d->handle==INVALID_HANDLE_VALUE){free(d);return NULL;} d->first=1; return d; }
int im_dir_next_ex(ImDir *d,char *name,size_t cap,int *isdir){ if(!d||!name||!cap)return 0; for(;;){ if(!d->first&&!FindNextFileA(d->handle,&d->data))return 0; d->first=0; if(strcmp(d->data.cFileName,".")&&strcmp(d->data.cFileName,"..")){strncpy(name,d->data.cFileName,cap-1);name[cap-1]=0;if(isdir)*isdir=(d->data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;return 1;} } }
int im_dir_next(ImDir *d,char *name,size_t cap){return im_dir_next_ex(d,name,cap,NULL);} void im_dir_close(ImDir *d){if(!d)return;if(d->handle!=INVALID_HANDLE_VALUE)FindClose(d->handle);free(d);}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
struct ImDir { DIR *handle; };
ImDir *im_dir_open(const char *path){ImDir*d=(ImDir*)calloc(1,sizeof*d);if(!d)return NULL;d->handle=opendir(path?path:".");if(!d->handle){free(d);return NULL;}return d;}
int im_dir_next_ex(ImDir*d,char*name,size_t cap,int*isdir){struct dirent*e;if(!d||!name||!cap)return 0;while((e=readdir(d->handle))){if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")){strncpy(name,e->d_name,cap-1);name[cap-1]=0;if(isdir){int dir=0;
#ifdef DT_DIR
dir = e->d_type == DT_DIR;
if (e->d_type == DT_UNKNOWN) { struct stat st; if (fstatat(dirfd(d->handle), e->d_name, &st, 0) == 0) dir = S_ISDIR(st.st_mode); }
#endif
*isdir=dir;}return 1;}}return 0;}
int im_dir_next(ImDir*d,char*n,size_t c){return im_dir_next_ex(d,n,c,NULL);}void im_dir_close(ImDir*d){if(!d)return;if(d->handle)closedir(d->handle);free(d);}
#endif
