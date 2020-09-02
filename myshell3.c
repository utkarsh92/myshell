#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LINE_LEN 500 //allowed input line length
#define WORD 20      //allowed no of arguments per command
#define WORD_LEN 30  //allowed length of 1 argument

int shmid;                                   //shared memory id
int *loop, *flag_grep[2];                    //shared memory variables
int grep_fd[2][2];                           //2d pipe fds
int flag_signal;                             //flag variable for signal handling
char tempFile[2][10] = {".temp1", ".temp2"}; //2d char array holding temp file names

//signal handler for SIGINT and SIGTERM
void handler(int num)
{
    flag_signal = 1;

    if (num == 2)
    {
        //SIGINT
        char msg[] = "\nthe program is interrupted, do you want to exit [Y/N] ";
        write(1, msg, strlen(msg));
        char c[1] = "";
        read(0, c, sizeof(c));
        if ((c[0] == 'Y') || (c[0] == 'y'))
        {
            *loop = 0;
        }
    }
    else if (num == 15)
    {
        //SIGTERM
        //TODO requires another enter to quit
        char msg[] = "\nGot SIGTERM-Leaving\n";
        write(1, msg, strlen(msg));
        *loop = 0;
    }
}

//Prints error message and exits
void parseError()
{
    char err[] = "Illegal command or arguments\n";
    write(1, err, strlen(err));
    exit(1);
}

//Gets total user & system ticks spent by CPU on given pid
void getUSTicks(int *user, int *system, char *pid)
{
    char fpath[30] = "";
    strcat(fpath, "/proc/");
    strcat(fpath, pid);
    strcat(fpath, "/stat");

    FILE *fp = fopen(fpath, "r");

    if (fp != NULL)
    {
        char *tempToken;
        char line[200];

        while (fgetc(fp) != ')')
            ;

        int x = 0;
        while (x < 200)
            line[x++] = fgetc(fp);

        tempToken = strtok(line, " ");
        for (int j = 0; j < 11; j++)
            tempToken = strtok(NULL, " ");

        *user = atoi(tempToken);
        tempToken = strtok(NULL, " ");
        *system = atoi(tempToken);

        fclose(fp);
    }
    else
    {
        // char err[] = "(Unable to open /proc/<pid>/stat)\n";
        // write(1, err, strlen(err));
        parseError();
    }
}

//Gets total ticks spent by CPU overall
void getTTicks(int *total)
{
    FILE *fp = fopen("/proc/stat", "r");

    if (fp != NULL)
    {
        int x = 0;
        char *tempToken;
        char line[200];

        while (x < 200)
            line[x++] = fgetc(fp);

        *total = 0;
        tempToken = strtok(line, "\n");
        tempToken = strtok(tempToken, " ");

        while ((tempToken = strtok(NULL, " ")) != NULL)
            *total = *total + atoi(tempToken);

        fclose(fp);
    }
    else
    {
        // char err[] = "(Unable to open /proc/stat)\n";
        // write(1, err, strlen(err));
        parseError();
    }
}

//ALWAYS RUN THIS IN A CHILD PROCESS
//handles >,>> and | redirection
//'int i' represts the index from which out IO could be present
void setupOutIO(int index, char cmd[][WORD_LEN], int i)
{
    if ((strcmp(cmd[i], ">") == 0) || (strcmp(cmd[i], ">>") == 0))
    {
        //output to file
        if (strcmp(cmd[i + 1], "") != 0)
        {
            int fd;
            if (strcmp(cmd[i], ">") == 0)
                fd = open(cmd[i + 1], O_RDWR | O_CREAT | O_TRUNC, 0777);
            else
                fd = open(cmd[i + 1], O_RDWR | O_CREAT | O_APPEND, 0777);

            if (fd == -1)
            {
                // char err[] = "(Unable to open output file...)\n";
                // write(1, err, strlen(err));
                parseError();
            }
            else
            {
                //all arguments are valid
                //send final output to given file
                dup2(fd, 1);
            }
        }
        else
        {
            // char err[] = "(No output file given...)\n";
            // write(1, err, strlen(err));
            parseError();
        }
    }
    else if ((strcmp(cmd[i], "|") == 0) && (strcmp(cmd[i + 1], "grep") == 0))
    {
        //grep from output
        if (strcmp(cmd[i + 2], "") != 0)
        {
            //send final output to grep pipe
            close(grep_fd[index][0]);
            dup2(grep_fd[index][1], 1);
            *flag_grep[index] = i + 2;
        }
        else
        {
            // char err[] = "(No pattern string given...)\n";
            // write(1, err, strlen(err));
            parseError();
        }
    }
}

//gets output from pipe and sends to grep command if required
void grepOut(int index, char cmd[][WORD_LEN])
{
    if (*flag_grep[index])
    {
        close(grep_fd[index][1]);
        dup2(grep_fd[index][0], 0);
        char *arg[] = {"grep", cmd[*flag_grep[index]], NULL};
        execvp("grep", arg);
        perror("execvp");
    }
}

//attempts to execute the given command
void execute(int index, char cmd[][WORD_LEN])
{
    *flag_grep[index] = 0; //flag whether pipe grep is used
    pipe(grep_fd[index]);  //open pipe for grep

    if (strcmp(cmd[0], "checkcpupercentage") == 0)
    {
        if (strcmp(cmd[1], "") == 0)
            parseError();

        int user_old, user_new, sys_old, sys_new, total_old, total_new;

        getUSTicks(&user_old, &sys_old, cmd[1]);
        getTTicks(&total_old);
        sleep(1);
        getUSTicks(&user_new, &sys_new, cmd[1]);
        getTTicks(&total_new);

        double ucpu = (double)(user_new - user_old) / (double)(total_new - total_old);
        ucpu *= 100;
        double scpu = (double)(sys_new - sys_old) / (double)(total_new - total_old);
        scpu *= 100;

        int ch;
        if (ch = fork())
        {
            //parent
            wait(0);
            grepOut(index, cmd);
        }
        else
        {
            //child
            setupOutIO(index, cmd, 2);
            printf("user mode cpu percentage: %d%%\n", (int)ucpu);
            printf("system mode cpu percentage: %d%%\n", (int)scpu);
            exit(0);
        }
    }
    else if (strcmp(cmd[0], "checkresidentmemory") == 0)
    {
        if (strcmp(cmd[1], "") == 0)
            parseError();

        int child, fd[2], status;
        pipe(fd);

        if (child = fork())
        {
            //parent
            waitpid(child, &status, 0);
            if (status > 0)
            {
                // char err[] = "(Child error...)\n";
                // write(1, err, strlen(err));
                parseError();
            }
            else
            {
                char output[10] = "";
                close(fd[1]); //close write
                read(fd[0], output, 10);
                if (strcmp(output, "") != 0)
                {
                    int ch;
                    if (ch = fork())
                    {
                        //parent
                        wait(0);
                        grepOut(index, cmd);
                    }
                    else
                    {
                        //child
                        setupOutIO(index, cmd, 2);
                        write(1, output, strlen(output));
                        exit(0);
                    }
                }
                else
                {
                    // char err[] = "(PID not found...)\n";
                    // write(1, err, strlen(err));
                    parseError();
                }
            }
        }
        else
        {
            //child
            close(fd[0]); //close read
            dup2(fd[1], 1);
            char *arg[] = {"ps", "-p", cmd[1], "-o", "rss", "--no-header", NULL};
            execvp("ps", arg);
            perror("execvp");
            exit(1);
        }
    }
    else if (strcmp(cmd[0], "listFiles") == 0)
    {
        if (strcmp(cmd[1], "") != 0)
            parseError();

        int child, fd, status;
        fd = open("files.txt", O_RDWR | O_CREAT, 0777);

        if (child = fork())
        {
            //parent
            waitpid(child, &status, 0);
            if (status > 0)
            {
                // char err[] = "(Child error...)\n";
                // write(1, err, strlen(err));
                parseError();
            }
        }
        else
        {
            //child
            dup2(fd, 1); //replace STDOUT with file
            char *arg[] = {"ls", NULL};
            execvp("ls", arg);
            perror("execvp");
            close(fd);
            exit(1);
        }

        close(fd);
    }
    else if (strcmp(cmd[0], "sortFile") == 0)
    {
        if (strcmp(cmd[1], "") == 0)
            parseError();

        int child, fd, status;
        fd = open(cmd[1], O_RDONLY);

        if (fd == -1)
        {
            // char err[] = "(Unable to open file...)\n";
            // write(1, err, strlen(err));
            parseError();
        }

        if (child = fork())
        {
            //parent
            waitpid(child, &status, 0);
            if (status > 0)
            {
                // char err[] = "(Child error...)\n";
                // write(1, err, strlen(err));
                close(fd);
                parseError();
            }
            else
            {
                grepOut(index, cmd);
            }
        }
        else
        {
            //child
            dup2(fd, 0); //replace STDIN with file
            setupOutIO(index, cmd, 2);
            char *arg[] = {"sort", NULL};
            execvp("sort", arg);
            perror("execvp");
            close(fd);
            exit(1);
        }

        close(fd);
    }
    else if (strcmp(cmd[0], "executeCommands") == 0)
    {
        if (strcmp(cmd[1], "") == 0)
            parseError();

        int child, status;

        if (child = fork())
        {
            //parent
            waitpid(child, &status, 0);

            if (status > 0)
            {
                // char err[] = "(Child error...)\n";
                // write(1, err, strlen(err));
                parseError();
            }
            else
            {
                //.temp file created

                if (child = fork())
                {
                    //parent
                    waitpid(child, &status, 0);

                    status = 0;
                    if (child = fork())
                    {
                        //parent
                        waitpid(child, &status, 0);
                        remove(tempFile[index]);
                        grepOut(index, cmd);
                    }
                    else
                    {
                        //child
                        //execute .temp file
                        int fd = open(tempFile[index], O_RDONLY);
                        dup2(fd, 0); //replace input of execution to .temp
                        setupOutIO(index, cmd, 2);
                        char *arg[] = {"myshell3", "stop", NULL};
                        execve("myshell3", arg, NULL);
                        perror("execve");
                        exit(1);
                    }
                }
                else
                {
                    //child
                    //append "exit" at the end of .temp file
                    int fd = open(tempFile[index], O_RDWR | O_APPEND);
                    dup2(fd, 1);
                    write(1, "exit", 4);
                    exit(0);
                }
            }
        }
        else
        {
            //child
            //create a copy .temp file
            char *arg[] = {"cp", cmd[1], tempFile[index], NULL};
            execvp("cp", arg);
            perror("execvp");
            exit(1);
        }
    }
    else if (strcmp(cmd[0], "cat") == 0)
    {
        if (strcmp(cmd[1], "") == 0)
            parseError();

        int child, fd, status;

        fd = open(cmd[1], O_RDONLY);
        if (fd == -1)
        {
            // char err[] = "(Unable to open file...)\n";
            // write(1, err, strlen(err));
            parseError();
        }

        if (child = fork())
        {
            //parent
            waitpid(child, &status, 0);
            if (status == 1)
            {
                // char err[] = "(Child error...)\n";
                // write(1, err, strlen(err));
                close(fd);
                parseError();
            }
            else
            {
                grepOut(index, cmd);
            }
        }
        else
        {
            //child
            setupOutIO(index, cmd, 2);
            char *arg[] = {"cat", cmd[1], NULL};
            execvp("cat", arg);
            perror("execvp");
            close(fd);
            exit(1);
        }

        close(fd);
    }
    else if (strcmp(cmd[0], "grep") == 0)
    {
        if (strcmp(cmd[1], "") == 0)
            parseError();

        if (strcmp(cmd[2], "<") != 0)
            parseError();

        if (strcmp(cmd[3], "") == 0)
            parseError();

        int child, fd, status;

        fd = open(cmd[3], O_RDONLY);
        if (fd == -1)
        {
            // char err[] = "(Unable to open file...)\n";
            // write(1, err, strlen(err));
            parseError();
        }

        if (child = fork())
        {
            //parent
            waitpid(child, &status, 0);
            if (status == 1)
            {
                // char err[] = "(Child error...)\n";
                // write(1, err, strlen(err));
                close(fd);
                parseError();
            }
            else
            {
                grepOut(index, cmd);
            }
        }
        else
        {
            //child
            setupOutIO(index, cmd, 4);
            char *arg[] = {"grep", cmd[1], cmd[3], NULL};
            execvp("grep", arg);
            perror("execvp");
            close(fd);
            exit(1);
        }

        close(fd);
    }
    else if (strcmp(cmd[0], "exit") == 0)
    {
        *loop = 0;
        exit(0);
    }
    else
    {
        //no syntax matched
        parseError();
    }
}

//breaks input into 2 commands by ';' as delimiter
//breaks commands in words/arguments by ' ' (whitespace) as delimiter
void parseInput(char *input, char cmd1[][WORD_LEN], char cmd2[][WORD_LEN], int *flag_parallel)
{
    char *tok;
    char line1[LINE_LEN] = "";
    char line2[LINE_LEN] = "";

    //break input to 2 cmds ';'
    tok = strtok(input, ";\n");
    strcpy(line1, tok);
    tok = strtok(NULL, ";\n");
    if (tok != NULL)
    {
        strcpy(line2, tok);
        *flag_parallel = 1;
    }

    //lines into words
    int i = 0;
    tok = strtok(line1, " ");
    while (tok != NULL)
    {
        strcpy(cmd1[i++], tok);
        tok = strtok(NULL, " ");
    }

    if (*flag_parallel)
    {
        i = 0;
        tok = strtok(line2, " ");
        while (tok != NULL)
        {
            strcpy(cmd2[i++], tok);
            tok = strtok(NULL, " ");
        }
    }
}

//outer shell of the program
int main(int argc, char const *argv[])
{
    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    //setting up shared memory and variables
    shmid = shmget(IPC_PRIVATE, 3 * sizeof(int), IPC_CREAT | 0666);
    loop = (int *)shmat(shmid, NULL, 0);
    *loop = 1;
    flag_grep[0] = loop + 1;
    flag_grep[1] = loop + 1;
    *flag_grep[0] = *flag_grep[1] = 0;

    int pid1, pid2;
    int flag_parallel;

    while (*loop)
    {
        char input[LINE_LEN] = {};
        char cmd1[WORD][WORD_LEN] = {{}};
        char cmd2[WORD][WORD_LEN] = {{}};

        flag_parallel = flag_signal = 0;

        //show prompt and read input
        if (argc <= 1)
            write(1, "\nmyShell> ", 10);
        int len = (int)read(0, input, LINE_LEN);

        //end loop if signal handled
        if (!(*loop) || flag_signal || (len == 1))
            continue;

        parseInput(input, cmd1, cmd2, &flag_parallel);

        if (!(pid1 = fork()))
        {
            //child0
            execute(0, cmd1);
            exit(0);
        }
        else if (flag_parallel && !(pid2 = fork()))
        {
            //child1
            execute(1, cmd2);
            exit(0);
        }
        else
        {
            //parent
            waitpid(pid1, NULL, 0);
            if (flag_parallel)
                waitpid(pid2, NULL, 0);
        }
    }

    return 0;
}