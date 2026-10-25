#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
int hbp_main(int argc,char **argv);
int wmain(int argc,wchar_t **wide){
    char **args=calloc((size_t)argc+1,sizeof(char*));if(!args)return 1;
    for(int i=0;i<argc;i++){
        int n=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,NULL,0,NULL,NULL);if(!n)return 1;
        args[i]=malloc(n);if(!args[i])return 1;
        if(!WideCharToMultiByte(CP_UTF8,0,wide[i],-1,args[i],n,NULL,NULL))return 1;
    }
    int result=hbp_main(argc,args);
    for(int i=0;i<argc;i++)free(args[i]);free(args);return result;
}
