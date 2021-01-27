#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <setjmp.h>

#define MAXCMDLEN 1024 // max command length
#define MAXARGLEN 128 // max argument word length
#define MAXARGCNT 50 // max argument count
#define MAXDIRLEN 128 // max directory length
#define MAXREDIRECTCNT 20 // max redirect count
#define MAXPIPECNT 16 // max pipe count
#define MAXBACKJOBCNT 128 // max background job count

jmp_buf buf;
pid_t mainpid;
char **Arglist;
int *Redirectpos, Argcnt=0;

struct job
{
    char cmd[MAXCMDLEN];
    int id;
    pid_t pid[MAXPIPECNT];
    int running[MAXPIPECNT];
    int pidcnt;
    char frontorback;
};

void jobinit(struct job *job)
{
    job->cmd[0]='\0';
    job->id=1;
    for (int i=0;i<MAXPIPECNT;++i)
    {
        job->pid[i]=0;
        job->running[i]=0;
    }
    job->pidcnt=0;
}

int isrunning(struct job job)
{
    for (int i=0;i<job.pidcnt;++i)
    {
        if (waitpid(job.pid[i],NULL,WNOHANG) == 0)
            return 1;
        else
            job.running[i]=1;
    }
    return 0;
}

void printbackjob(struct job *jobs, const int cntjob)
{
    for (int i=0;i<cntjob;++i)
    {
        if (jobs[i].frontorback == 'b' && jobs[i].pid[0] != 0)
        {
            printf("[%d] ",jobs[i].id);
            fflush(stdout);
            if (isrunning(jobs[i]))
                printf("running ");
            else
                printf("done ");
            fflush(stdout);
            printf("%s\n",jobs[i].cmd);
            fflush(stdout);
        }
    }
}

void display1(int *arr, int len)
{
    for (int i=0;i<len;++i)
        printf("%d ",arr[i]);
    printf("\n");
}

void display2(char **arg, int argcnt)
{
    for (int i=0;i<argcnt;++i)
    {
        for (int j=0;j<MAXARGLEN;++j)
            printf("%c",arg[i][j]);
        printf("\n");
    }
}

int countchar(const char *str, char ch)
{
    int cnt=0;
    for (int i=0;i<strlen(str);++i)
    {
        if (str[i] == ch)
            cnt++;
    }
    return cnt;
}

int findecho(char **arglist, const int pos)
{
    for (int i=pos-1;i>=0;--i)
    {
        if (strcmp(arglist[i],"|") == 0)
            break;
        if (strcmp(arglist[i],"echo") == 0 || strcmp(arglist[i],"\"echo\"") == 0 || strcmp(arglist[i],"\'echo\'") == 0)
            return 1;
    }
    return 0;
}

int embraced(const char *cmd, const int charpos)
{
    int fqpos=-1;
    if (cmd[charpos] == ' ' && cmd[charpos-1] == cmd[charpos+1] &&
        (cmd[charpos-1] == '\"' || cmd[charpos-1] == '\''))
        return 0;
    if (cmd[charpos] == ' ' && ((cmd[charpos-1] == '\"' && cmd[charpos+1] == '\'') ||
        (cmd[charpos-1] == '\'' && cmd[charpos+1] == '\"')))
        return 0;
    for (int i=charpos-1;i>=0;--i)
    {
        if (cmd[i] == ' ')
            break;
        if (cmd[i] == '\"' || cmd[i] == '\'')
            fqpos=i;
    }
    for (int i=charpos+1;i<strlen(cmd);++i)
    {
        if (cmd[i] == ' ')
            break;
        if (fqpos!= -1 && cmd[i] == cmd[fqpos])
            return 1;
    }
    return 0;
}

void parsecmd(const char *cmd, char **arglist, int *argcnt)
{
    int i=0, j=0, k=0;
    // skip space first
    while (cmd[k] == ' ')
        k++;

    while (cmd[k] != '\n')
    {
        if (cmd[k] == ' ' && findecho(arglist,i) == 1 && embraced(cmd,k) != 0 &&
            (arglist[i][0] == '\"' || arglist[i][0] == '\''))
        {
            arglist[i][j]=cmd[k];
            j++, k++;
            continue;
        }
        if (cmd[k] == ' ' && embraced(cmd,k) == 0)
        {
            arglist[i][j+1]='\0';
            k++, i++, j=0;
            continue;
        }
        if (k>0 && cmd[k] == '<' && embraced(cmd,k) == 1)
        {
            arglist[i][j]=cmd[k];
            j++, k++;
            continue;
        }
        // redirect symbol with no space
        if (k<MAXCMDLEN-1 && cmd[k] != ' ' && cmd[k] != '>' && cmd[k] != '\"'
            && (cmd[k+1] == '>' || cmd[k+1] == '<'))
        {
            arglist[i][j]=cmd[k];
            arglist[i][j+1]='\0';
            k++, i++, j=0;
            continue;
        }
        if ((k<MAXCMDLEN-1 && (cmd[k] == '>' || cmd[k] == '<') &&
            cmd[k+1] != ' ' && cmd[k+1] != '>' && cmd[k+1] != '\"') ||
            (k == 0 && strlen(cmd) == 1))
        {
            if (k>0 && cmd[k-1] == '\"')
            {
                arglist[i][j]=cmd[k];
                j++, k++;
                continue;
            }
            arglist[i][j]=cmd[k];
            arglist[i][j+1]='\0';
            k++, i++, j=0;
            continue;
        }

        arglist[i][j]=cmd[k];

        if (k>0 && cmd[k] == '>' && cmd[k-1] == '>')
        {
            j++, k++;
            continue;
        }
        j++, k++;
    }
    if (strlen(arglist[i]) != 0)
        *argcnt=i+1;
    else
        *argcnt=i;
}

int detectquotes(char **arglist, const int pos, int *index)
{
    char *str=arglist[pos];
    int embrace=0;
    int cntsingle=countchar(str,'\'');
    int cntdouble=countchar(str,'\"');

    for (int j=0;j<strlen(str);++j)
    {
        if (str[j]  == '\"')
            index[j]=2;
        else if (str[j]  == '\'')
            index[j]=1;
        else
            index[j]=0;
    }
    if (cntsingle == 0 && cntdouble == 0)
        return 0;

    if (cntsingle%2 == 1 || cntdouble%2 == 1)
    {
        if ((cntdouble == 1 || cntsingle == 1) && (Arglist[pos][0] == '\"' || Arglist[pos][0] == '\''))
        {
            Arglist[pos][strlen(Arglist[pos])]='\n';
            return 2;
        }
        return 1;
    }

    if (cntsingle == 0 || cntdouble == 0)
    {
        for (int j=0;j<strlen(str);++j)
        {
            if (index[j] == 1 || index[j] == 2)
                index[j]=-1;
        }
    }
    else
    {
        int left=0, right=(int)strlen(str)-1;
        while (left <= right)
        {
            while (index[left] == 0 )
                left++;

            while (index[right] == 0)
                right--;

            if (index[left] == index[right])
            {
                if (embrace == 0 || embrace == index[left])
                {
                    index[left]=index[right]=-1;
                    embrace=index[right];
                }
                left++, right--;
            }
            else
            {
                return 1;
            }
        }
    }
    return 0;
}

void processquotes(char **arglist, const int pos, const int *index)
{
    char newarg[MAXARGLEN]={'\0'};
    int cntnew=0;
    for (int j=0;j<strlen(arglist[pos]);++j)
    {
        if (index[j] != -1)
            newarg[cntnew++]=arglist[pos][j];
    }
    strncpy(arglist[pos],newarg,strlen(arglist[pos]));
}

int buildincmd(char **arglist, const int argcnt, char *lastdir)
{
    if (strcmp(arglist[0],"pwd") == 0)
    {
        char dirname[MAXDIRLEN];
        if (getcwd(dirname,sizeof(dirname)) == NULL)
        {
            printf("Cannot get current directory\n");
            fflush(stdout);
            return 1;
        }
        printf("%s\n",dirname);
        fflush(stdout);
        return 0;
    }

    if (strcmp(arglist[0],"cd") == 0)
    {
        if (argcnt<2)
        {
            char *homedir=getenv("HOME");
            chdir(homedir);
        }
        else if (argcnt>2)
        {
            fprintf(stderr,"Too many arguments\n");
        }
        else
        {
            int used=0;
            if (strcmp(arglist[1],"-") == 0)
            {
                strcpy(arglist[1],lastdir);
                used=1;
            }
            if (chdir(arglist[1])<0)
            {
                printf("%s: No such file or directory\n",arglist[1]);
                fflush(stdout);
            }
            if (used == 1)
                printf("%s\n",lastdir);
        }
    }
    return 0;
}

int redirectcmd(char **arglist, const int *redirectpos, int argcnt, int redirectcnt, char *lastdir)
{
    int rdincnt=0, cnt=0, index=0;
    char *cmdnord[argcnt-redirectcnt+1];
    // copy the rest command without redirection symbol
    while (index<argcnt)
    {
        if (strcmp(arglist[index],"<") == 0 || strcmp(arglist[index],">") == 0 || strcmp(arglist[index],">>") == 0)
        {
            index+=2;
            continue;
        }
        else
        {
            cmdnord[cnt++]=arglist[index];
            index++;
        }
    }
    for (int i=0;i<redirectcnt;++i)
    {
        if (strcmp(arglist[redirectpos[i]],">") == 0)
        {
            int fp=open(arglist[redirectpos[i]+1],O_CREAT|O_WRONLY|O_TRUNC,0644);
            if (fp<0)
            {
                printf("%s: Permission denied\n", arglist[redirectpos[i]+1]);
                fflush(stdout);
                exit(1);
            }
            int fd=dup2(fp,STDOUT_FILENO);
            if (fd<0)
            {
                printf("Cannot duplicate file descriptor\n");
                fflush(stdout);
            }
            close(fp);
        }
        else if (strcmp(arglist[redirectpos[i]],">>") == 0)
        {
            int fp=open(arglist[redirectpos[i]+1],O_WRONLY|O_CREAT|O_APPEND,0644);
            if (fp<0)
            {
                printf("%s: Permission denied\n", arglist[redirectpos[i]+1]);
                fflush(stdout);
                exit(1);
            }
            int fd=dup2(fp,STDOUT_FILENO);
            if (fd<0)
            {
                printf("Cannot duplicate file descriptor\n");
                fflush(stdout);
            }
            close(fp);
        }
        else
        {
            int fp=open(arglist[redirectpos[i]+1],O_RDONLY,0644);
            if (fp<0)
            {
                printf("%s: No such file or directory\n",arglist[redirectpos[i]+1]);
                fflush(stdout);
                exit(1);
            }
            int fd=dup2(fp,STDIN_FILENO);
            if (fd<0)
            {
                printf("Cannot duplicate file descriptor\n");
                fflush(stdout);
            }
            rdincnt++;
            close(fp);
        }
    }
    cmdnord[cnt]=NULL;
    // consider buildin command
    if (strcmp(cmdnord[0],"cd") == 0 || strcmp(cmdnord[0],"pwd") == 0)
    {
        buildincmd(cmdnord,cnt,lastdir);
        exit(0);
    }
    else if (execvp(cmdnord[0],cmdnord)<0)
    {
        printf("%s: command not found\n", arglist[0]);
        fflush(stdout);
        exit(0);
    }
    return 0;
}

int singlecmd(char **arglist, int *redirectpos, int argcnt, int redirectcnt,
              struct job *job, struct job *joblist, int jobcnt, char *lastdir)
{
    int in=dup(STDIN_FILENO);
    int out=dup(STDOUT_FILENO);

    pid_t pid=fork();
    if (pid == -1)
    {
        printf("Cannot fork.\n");
        fflush(stdout);
        return 1;
    }
    else if (pid == 0)
    {
        if (job->frontorback == 'f')
            setpgid(0,mainpid);
        if (redirectcnt == 0)
        {
            // add null to end of arguments
            char *newarglist[argcnt+1];
            for (int i=0;i<argcnt;++i)
                newarglist[i]=arglist[i];

            newarglist[argcnt]=NULL;

            if((strcmp(arglist[0],"pwd") == 0) || (strcmp(arglist[0],"cd") == 0))
            {
                buildincmd(arglist,argcnt,lastdir);
                exit(0);
            }
            else if (execvp(newarglist[0],newarglist)<0)
            {
                printf("%s: command not found\n", newarglist[0]);
                fflush(stdout);
                exit(0);
            }
        }
        else
        {
            redirectcmd(arglist,redirectpos,argcnt,redirectcnt,lastdir);
        }
    }
    else
    {
        if (job->frontorback == 'f')
        {
            waitpid(pid,NULL,0);
        }
        else
        {
            job->pid[0]=pid;
            job->pidcnt=1;
            joblist[jobcnt]=*job;
            waitpid(pid,NULL,WNOHANG|WUNTRACED);
        }
    }
    dup2(in,STDIN_FILENO);
    dup2(out,STDOUT_FILENO);
    return 0;
}

int pipecmd(char **arglist, int left, int right, const int *pipepos,
            struct job *job, struct job *joblist, int jobcnt, char *lastdir)
{
    if (left >= right)
        return 0;

    int newrdpos[MAXREDIRECTCNT];
    char *cmdnopipe[MAXARGCNT];
    int newrdcnt=0, pipeindex=-1;

    for (int i=left;i<right;++i)
    {
        if (strcmp(arglist[i],"|") == 0 && pipepos[i] == 2)
        {
            pipeindex=i;
            break;
        }
    }
    if (pipeindex == -1)
    {
        for (int i=0;i<right-left;++i)
        {
            cmdnopipe[i]=arglist[left+i];
            if (arglist[left+i][0] == '>' || arglist[left+i][0] == '<')
                newrdpos[newrdcnt++]=i;
        }
        return singlecmd(cmdnopipe,newrdpos,right-left,newrdcnt,job,joblist,jobcnt,lastdir);
    }
    else
    {
        for (int i=0;i<pipeindex-left;++i)
        {
            cmdnopipe[i]=arglist[left+i];
            if (arglist[left+i][0] == '>' || arglist[left+i][0] == '<')
            {
                newrdpos[newrdcnt++]=i;
            }
        }
        newrdpos[newrdcnt]=pipeindex-left;
        cmdnopipe[pipeindex-left]=NULL;
    }

    int fd[2];
    if (pipe(fd) == -1)
    {
        printf("Cannot pipe\n");
        fflush(stdout);
        return 1;
    }
    pid_t pid=vfork();
    if (pid == -1)
    {
        printf("Cannot fork\n");
        fflush(stdout);
        return 1;
    }
    else if (pid == 0)
    {
        close(fd[0]);
        dup2(fd[1],STDOUT_FILENO);
        close(fd[1]);
        if (newrdcnt == 0)
        {
            if (strcmp(cmdnopipe[0],"pwd") == 0 || strcmp(cmdnopipe[0],"cd") == 0)
            {
                buildincmd(cmdnopipe,pipeindex-left,lastdir);
                exit(0);
            }
            else
            {
                if (execvp(cmdnopipe[0],cmdnopipe)<0)
                {
                    printf("%s: command not found\n",cmdnopipe[0]);
                    fflush(stdout);
                    exit(0);
                }
            }
        }
        else
        {
            redirectcmd(cmdnopipe,newrdpos,pipeindex-left,newrdcnt,lastdir);
        }
    }
    else
    {
        if (job->frontorback == 'b')
        {
            int cntp=0;
            for (int i=0;i<pipeindex;++i)
            {
                if (pipepos[i] == 2)
                    cntp++;
            }
            job->pid[cntp]=pid;
            joblist[jobcnt]=*job;
        }
        if (pipeindex+1<right)
        {
            close(fd[1]);
            dup2(fd[0],STDIN_FILENO);
            close(fd[0]);
            // recursive call
            pipecmd(arglist,pipeindex+1,right,pipepos,job,joblist,jobcnt,lastdir);
        }
        if (job->frontorback == 'f')
            waitpid(pid,NULL,0);
        else
            waitpid(pid,&job->running[0],WNOHANG);
    }
    return 0;
}

int pipeline(char **arglist, int *redirectpos, int *pipepos, int argcnt,
             int redirectcnt, struct job *job, struct job *joblist, int jobcnt, int cmdcnt, char *lastdir)
{
    int in=dup(STDIN_FILENO);
    int out=dup(STDOUT_FILENO);

    if (job->frontorback == 'b')
    {
        job->pidcnt=cmdcnt;
        joblist[jobcnt]=*job;
    }

    int issingle=1;
    for (int i=0;i<argcnt;++i)
    {
        if (countchar(arglist[i],'|') != 0)
        {
            issingle=0;
            break;
        }
    }
    if (issingle == 1)
        return singlecmd(arglist,redirectpos,argcnt,redirectcnt,job,joblist,jobcnt,lastdir);

    pipecmd(arglist,0,argcnt,pipepos,job,joblist,jobcnt,lastdir);

    dup2(in,STDIN_FILENO);
    dup2(out,STDOUT_FILENO);
    return 0;
}

int checkunfinish(char **arglist, const int argcnt)
{
    if (strcmp(arglist[argcnt-1],">") == 0 || strcmp(arglist[argcnt-1],"<") == 0 ||
        strcmp(arglist[argcnt-1],"|") == 0)
    {
        fflush(stdout);
        return 1;
    }
    return 0;
}

int checkrdandmiss(char **arglist, const int argcnt, const int redirectcnt)
{
    int rdinpos=-1, rdoutpos=-1, rdoutcnt=0;
    for (int i=0;i<argcnt;++i)
    {
        if (strcmp(arglist[i],">") == 0 || strcmp(arglist[i],">>") == 0)
        {
            rdoutpos=i;
            rdoutcnt++;
        }
        if (strcmp(arglist[i],"<") == 0)
        {
            rdinpos=i;
        }
    }
    if (rdoutcnt>1)
    {
        printf("error: duplicated output redirection\n");
        fflush(stdout);
        return 1;
    }
    if (redirectcnt-rdoutcnt>1)
    {
        printf("error: duplicated input redirection\n");
        fflush(stdout);
        return 1;
    }
    for (int i=0;i<argcnt;++i)
    {
        if (rdinpos != -1 && strcmp(arglist[i],"|") == 0 && i<rdinpos)
        {
            printf("error: duplicated input redirection\n");
            fflush(stdout);
            return 1;
        }
        if (rdoutpos != -1 && strcmp(arglist[i],"|") == 0 && i>rdoutpos)
        {
            printf("error: duplicated output redirection\n");
            fflush(stdout);
            return 1;
        }
    }
    return 0;
}

void freememory(char **arglist, int *redirectpos)
{
    for (int i=0;i<MAXARGCNT;++i)
    {
        free(*(arglist+i));
    }
    free(arglist);
    free(redirectpos);
}

void stop()
{
    printf("\n");
    siglongjmp(buf,2);
}

int main()
{
    int in=dup(STDIN_FILENO);
    int out=dup(STDOUT_FILENO);
    int cntjob=0;
    char lastdir[MAXDIRLEN];
    char currentdir[MAXDIRLEN];

    struct job joblist[MAXBACKJOBCNT];
    for (int i=0;i<MAXBACKJOBCNT;++i)
        jobinit(&joblist[i]);

    mainpid=getpid();
    setpgid(0,0);
    getcwd(lastdir,sizeof(lastdir));
    getcwd(currentdir,sizeof(currentdir));

    // CTRL-C support
    signal(SIGINT,stop);
    if (sigsetjmp(buf,1) == 2)
    {
        freememory(Arglist,Redirectpos);
        dup2(in,STDIN_FILENO);
        dup2(out,STDOUT_FILENO);
    }
    while (1)
    {
        fflush(stdin);
        printf("mumsh $ ");
        fflush(stdout);
        int redirectcnt=0, cmdcharcnt=0, cmdextracnt=0, pipecmdcnt=0;
        Argcnt=0;
        //initialize command buffer
        char cmd[MAXCMDLEN];
        for (int i=0;i<MAXCMDLEN;++i)
            cmd[i]='\0';
        // create and initialize 2d argument list
        Arglist=(char **)malloc(sizeof(char *)*MAXARGCNT);
        for (int i=0;i<MAXARGCNT;++i)
        {
            *(Arglist+i)=(char *)malloc(sizeof(char)*MAXARGLEN);
        }
        for (int i=0;i<MAXARGCNT;++i)
        {
            for (int j=0;j<MAXARGLEN;++j)
                Arglist[i][j]='\0';
        }
        // create and initialize redirect symbol position
        Redirectpos=(int *)malloc(sizeof(int)*MAXREDIRECTCNT);
        int pippos[MAXARGCNT]={0};
        for (int i=0;i<MAXREDIRECTCNT;++i)
            Redirectpos[i]=0;
        // create index for quotes
        int index[MAXARGCNT][MAXARGLEN]={0};
        // read command
        char cmdtemp;
        while ((cmdtemp=(char)fgetc(stdin)) != '\n')
        {
            // CTRL-D support
            if (cmdtemp == EOF)
            {
                printf("exit\n");
                fflush(stdout);
                freememory(Arglist,Redirectpos);
                return 0;
            }
            cmd[cmdcharcnt++]=cmdtemp;
        }
        cmd[cmdcharcnt]='\n';
        // trivial case
        if ((strncmp(cmd,"exit",4) == 0) && (cmd[4] == '\n'))
        {
            printf("exit\n");
            fflush(stdout);
            freememory(Arglist,Redirectpos);
            break;
        }
        if ((strncmp(cmd,"jobs",4) == 0) && (cmd[4] == '\n'))
        {
            printbackjob(joblist,cntjob);
            freememory(Arglist,Redirectpos);
            continue;
        }
        if (cmd[0] == '\n')
        {
            freememory(Arglist,Redirectpos);
            continue;
        }
        // create and initialize job to store background jobs
        struct job currentjob;
        jobinit(&currentjob);
        for (int i=0;i<MAXCMDLEN;++i)
        {
            currentjob.cmd[i]=cmd[i];
        }
        currentjob.id=cntjob+1;
        if (cmd[cmdcharcnt-1] == '&')
        {
            cmd[cmdcharcnt-1]='\n';
            cmd[cmdcharcnt]='\0';
            currentjob.frontorback='b';
            printf("[%d] ",currentjob.id);
            printf("%s\n",currentjob.cmd);
            fflush(stdout);
        }
        else
        {
            currentjob.frontorback='f';
        }
        // first parse
        parsecmd(cmd,Arglist,&Argcnt);
        unsigned long lastindex=strlen(Arglist[Argcnt-1]), cnt=lastindex;
        int stop=Argcnt, hq, cf;
        // syntax error
        int syntaxerror=0;
        for (int i=0;i<Argcnt;++i)
        {
            if (i<Argcnt-1 && strcmp(Arglist[i],">") == 0 &&
                (Arglist[i+1][0] == '>' || Arglist[i+1][0] == '<' || Arglist[i+1][0] == '|'))
            {
                printf("syntax error near unexpected token `%c\'\n",Arglist[i+1][0]);
                fflush(stdout);
                syntaxerror=1;
                break;
            }
            if ((i>0 && strcmp(Arglist[i],Arglist[i-1]) == 0 && strcmp(Arglist[i],"|") == 0 &&
                 findecho(Arglist,i) == 0) || (i == 0 && strcmp(Arglist[i],"|") == 0))
            {
                printf("error: missing program\n");
                fflush(stdout);
                syntaxerror=1;
                break;
            }
        }
        if (syntaxerror == 1)
        {
            freememory(Arglist,Redirectpos);
            continue;
        }

        for (int i=0;i<Argcnt-1;++i)
            detectquotes(Arglist,i,index[i]);

        while ((hq=detectquotes(Arglist,Argcnt-1,index[Argcnt-1]))>0 ||
            (cf=checkunfinish(Arglist,Argcnt)) == 1)
        {
            printf("> ");
            fflush(stdout);
            if (hq>0)
            {
                if (hq == 2)
                    cnt++;
                //printf("run1\n");
                char prev='*';
                while ((cmdtemp=(char)fgetc(stdin)) != '\n')
                {
                    if (cmdtemp == EOF)
                    {
                        printf("exit\n");
                        fflush(stdout);
                        freememory(Arglist,Redirectpos);
                        return 0;
                    }
                    if (prev == ' ' && (cmdtemp == '>' || cmdtemp == '<' || cmdtemp == '|'))
                    {
                        Arglist[Argcnt-1][cnt-1]='\0';
                        Argcnt++, cnt=0;
                        Arglist[Argcnt-1][cnt++]=prev=cmdtemp;
                        continue;
                    }
                    if ((prev == '>' || prev == '<' || prev == '|') && cmdtemp == ' ')
                    {
                        Argcnt++, cnt=0, prev=cmdtemp;
                        continue;
                    }
                    Arglist[Argcnt-1][cnt++]=prev=cmdtemp;
                }
                if (strlen(Arglist[Argcnt-1]) == lastindex)
                    continue;
            }
            if (cf == 1 && hq != 1)
            {
                //printf("run2\n");
                char prev='*';
                while ((cmdtemp=(char)fgetc(stdin)) != '\n')
                {
                    if (cmdtemp == EOF)
                    {
                        printf("exit\n");
                        fflush(stdout);
                        freememory(Arglist,Redirectpos);
                        return 0;
                    }
                    if (cmdtemp == ' ')
                    {
                        prev=cmdtemp;
                        continue;
                    }
                    if (prev == ' ')
                        Argcnt++, cmdextracnt=0;

                    Arglist[Argcnt][cmdextracnt++]=prev=cmdtemp;
                }
                if (strlen(Arglist[Argcnt]) == 0)
                    continue;

                Argcnt++, cmdextracnt=0;
            }
        }

        if (Arglist[stop-1][0] == Arglist[stop-1][strlen(Arglist[stop-1])-1]
            && (Arglist[stop-1][0] == '\"' || Arglist[stop-1][0] == '\''))
            detectquotes(Arglist,stop-1,index[stop-1]);

        for (int i=stop;i<Argcnt;++i)
            detectquotes(Arglist,i,index[i]);

        for (int i=0;i<Argcnt;++i)
        {
            if (strcmp(Arglist[i],"\"|\"") == 0 || strcmp(Arglist[i],"\'|\'") == 0)
                pippos[i]=1;
            if (strcmp(Arglist[i],"|") == 0)
            {
                pippos[i]=2;
                pipecmdcnt++;
            }
        }

        for (int i=0;i<Argcnt;++i)
            processquotes(Arglist,i,index[i]);

        // count redirect and position
        for (int i=0;i<Argcnt;++i)
        {
            if (strcmp(Arglist[i],"<") == 0 || strcmp(Arglist[i],">") == 0 || strcmp(Arglist[i],">>") == 0)
                Redirectpos[redirectcnt++]=i;
        }
        Redirectpos[redirectcnt]=Argcnt;
        // check redirection and missing error
        if (checkrdandmiss(Arglist,Argcnt,redirectcnt) == 1)
        {
            freememory(Arglist,Redirectpos);
            continue;
        }
        //printf("-------------------------\n");
        //display2(Arglist,Argcnt);
        //display1(pippos,Argcnt);
        // execute command
        if (redirectcnt == 0 && (strcmp(Arglist[0],"cd") == 0 || strcmp(Arglist[0],"pwd") == 0))
        {
            buildincmd(Arglist,Argcnt,lastdir);
        }
        else
        {
            pipeline(Arglist,Redirectpos,pippos,Argcnt,redirectcnt,&currentjob,joblist,cntjob,pipecmdcnt+1,lastdir);
            if (currentjob.frontorback == 'b')
                joblist[cntjob++]=currentjob;
        }

        char dirtemp[MAXDIRLEN];
        getcwd(dirtemp,sizeof(dirtemp));
        if (strcmp(dirtemp,currentdir) != 0)
        {
            strncpy(lastdir,currentdir,sizeof(lastdir));
            strncpy(currentdir,dirtemp,sizeof(currentdir));
        }

        freememory(Arglist,Redirectpos);
    }
    return 0;
}
