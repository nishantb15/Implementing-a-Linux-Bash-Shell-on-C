/*
 * CS 241 - Spring 2020
 */
#include "format.h"
#include "shell.h"
#include "vector.h"
#include "sstring.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

typedef struct process {
    char *command;
    pid_t pid; 
} process;

int digits(char *pid) {
    char c[256];
    strcpy(c, pid);
    size_t i = 0;
    while (c[i]!='\0') {
        if (!isdigit(c[i])) {
            return -1;
        }
        i++;
    }
    return 0;
}
void* process_copy_constructor(void *other) {
    process* p = malloc(sizeof(process));
    p->command = strdup(((process*)other)->command);
    p->pid = ((process*)other)->pid;
    return p;
}

void* process_default_constructor(void) {
    void *p = malloc(sizeof(process));
    //p->command = strdup(command);
    //p->pid = pid;
    return p;
}

void process_destructor(void* p) {
    if (((process*)p)->command != NULL)
        free(((process*)p)->command);
    ((process*)p)->pid = 0;
    if (p != NULL)
        free(p);
}
extern char* optarg;
extern int opterr, optind, optopt;

//static int printPrompt = 0;

static FILE *history_file_name = NULL;
static pid_t parentpid = 0;
static pid_t childpid = 0;
static int dupf = 0;
static FILE *fname = NULL;
static int stdoutCopy = 0;
static int stdinCopy = 1;
static vector *processVec = NULL;
void parse_command(char *command, vector *historyCmds);

void parse_external_command(char* command) {
    //pid_t = fork();
    // use execvp(process name, processname + args);
    parentpid = getpid();
    fflush(stdout);
    pid_t pid = fork();
 
    int bg = 0;
    size_t cmdSize = strlen(command);
    char input[256];
    strcpy(input, command);
    if (input[cmdSize-1]=='&') {
        bg = 1;
        input[cmdSize-1] = '\0';
        input[cmdSize-2]= '\0';
    }

    if (pid < 0) {
        print_fork_failed();
    } else if (pid > 0) {
        // we are the parent
        process *p = (process*)malloc(sizeof(process));
        p->command = strdup(command);
        //printf("command = %s\n", p->command);
        p->pid = pid;
        vector_push_back(processVec, p);
        free(p->command);
        free(p);
        if (bg == 1) {
            return;
        }
        int statuss = 0;
        waitpid(pid, &statuss, 0);
        if (WIFEXITED(statuss)) {
            int ret = WEXITSTATUS(statuss);
            if (ret != 0) {
                print_wait_failed();
                return;
            }
        }
    } else {
        // we are the child
        childpid = getpid(); 
        //parentpid = childpid;
        int r  = -2;
        if (bg == 1) {
            r = setpgid(childpid,0);
        }
        if (r == -1) {
            print_setpgid_failed();
        }
        print_command_executed(childpid);
    
        // exec
        sstring *s = cstr_to_sstring(input);
        vector *v = sstring_split(s, ' ');

        char **argv = (char**)malloc(10000);
        size_t ss = vector_size(v);

        for (size_t i = 0; i < ss; i++) {
            argv[i] = *(char**)vector_at(v,i);
        }
        argv[ss] = NULL;
        execvp(argv[0], argv);
        print_invalid_command(command);
        print_exec_failed(command);
        vector_destroy(v);
        sstring_destroy(s);
    
        //for (size_t i = 0; i < ss; i++) {
        //    free(argv[i]);
        //}
        free(argv);
        exit(1);
    }
    return; 
}

int parse_external_command_logic(char *command) {
    parentpid = getpid();
    fflush(stdout);
    pid_t pid = fork();

    if (pid < 0) {
        print_fork_failed();
        return 0;
    } else if (pid > 0) {
        process *p = process_default_constructor();
        p->command = strdup(command);
        p->pid = pid;
        vector_push_back(processVec, p);
        free(p->command);
        free(p);
        // we are the parent
        int statuss = 0;
        waitpid(pid, &statuss, 0);
        if (WIFEXITED(statuss)) {
            int ret = WEXITSTATUS(statuss);
            if (ret != 0) {
                print_wait_failed();
                return 0;
            }
        }
        return 1;
    } else {
        // we are the child
        childpid = getpid();
        print_command_executed(childpid);
    
        if (dupf == 1) {
            dup2(fileno(fname), 1);
        } else if (dupf == 2) {
            dup2(fileno(fname), 0);
        }
        // exec
        sstring *s = cstr_to_sstring(command);
        vector *v = sstring_split(s, ' ');

        char **argv = (char**)malloc(10000);
        size_t ss = vector_size(v);

        for (size_t i = 0; i < ss; i++) {
            argv[i] = *(char**)vector_at(v,i);
        }
        argv[ss] = NULL;
        execvp(argv[0], argv);
        dup2(stdoutCopy, 1);
        dup2(stdinCopy, 0);
        //fclose(fname);
        print_invalid_command(command);
        print_exec_failed(command);
        vector_destroy(v);
        sstring_destroy(s);
    
        //for (size_t i = 0; i < ss; i++) {
        //    free(argv[i]);
        //}
        free(argv);
        exit(1);
    }
    return 0;
}

int parse_command_no_history(char* command) {
    if (command == NULL) return 0;
    sstring *s = cstr_to_sstring(command);
    vector *split = sstring_split(s, ' ');

    //int sig = 0;
    if (strcmp("kill", *(char**)vector_at(split,0))==0) {
        //printf("AT KILL\n");
        //sig = 1;
       
        if (vector_size(split) < 2 || strcmp(*(char**)vector_at(split,1),"")==0) {
            print_invalid_command(command);
            sstring_destroy(s);
            vector_destroy(split);
            return 0;
        } else {
            //printf("vecc = %s\n", *(char**)vector_at(split, 1));
            if (digits(*(char**)vector_at(split,1))==-1) {
                print_invalid_command(command);
                sstring_destroy(s);
                vector_destroy(split);
                return 0;
            }
            pid_t pid = atoi(*(char**)vector_at(split,1));
            //printf("pid = %d\n", pid);
            int pidExists = 0;
            size_t i = 0;
            while (i < vector_size(processVec)) {
                process *p = *(process**)vector_at(processVec, i); 
                if (p->pid == pid) {
                    char fn[256];
                    sprintf(fn, "/proc/%d/stat", p->pid);
                    FILE *stat = fopen(fn, "r");
                    if (stat == NULL) {
                        pidExists = 0;
                        // remove from processVec
                        vector_erase(processVec, i);
                        //fclose(stat);
                        break;
                    } else {
                        pidExists = 1;
                        fclose(stat);
                        break;
                    }
                }
                i++;
            }
            int stat = -1;
            if (pidExists == 1)
                stat = kill(pid, SIGTERM);
            if (stat == -1 || pidExists == 0) {
                print_no_process_found(pid);
                sstring_destroy(s);
                vector_destroy(split);
                return 0;
            } else {
                char c[250];
                size_t i = 0;
                while (i < vector_size(processVec)) {
                    process *p = *(process**)vector_at(processVec, i);
                    if (p->pid == pid) {
                        strcpy(c, p->command);
                        break;
                    } 
                    i++;
                }
                print_killed_process(pid, c);
                vector_erase(processVec, i);
                sstring_destroy(s);
                vector_destroy(split);
                return 1;
            }
        }
    } else if (strcmp("stop", *(char**)vector_at(split, 0))==0) {
         //sig = 1;
       
        if (vector_size(split) < 2 || strcmp(*(char**)vector_at(split,1),"")==0) {
            print_invalid_command(command);
            sstring_destroy(s);
            vector_destroy(split);
            return 0;
        } else {
            //printf("vecc = %s\n", *(char**)vector_at(split, 1));
            if (digits(*(char**)vector_at(split,1))==-1) {
                print_invalid_command(command);
                sstring_destroy(s);
                vector_destroy(split);
                return 0;
            }
            pid_t pid = atoi(*(char**)vector_at(split,1));
            //printf("pid = %d\n", pid);
            int stat = -1;
            int pidExists = 0;
            size_t i = 0;
            while (i < vector_size(processVec)) {
                process *p = *(process**)vector_at(processVec, i); 
                if (p->pid == pid) {
                    char fn[256];
                    sprintf(fn, "/proc/%d/stat", p->pid);
                    FILE *stat = fopen(fn, "r");
                    if (stat == NULL) {
                        pidExists = 0;
                        // remove from processVec
                        vector_erase(processVec, i);
                        //fclose(stat);
                        break;
                    } else {
                        pidExists = 1;
                        fclose(stat);
                        break;
                    }
                }
                i++;
            }
            if (pidExists == 1) {
                // TODO: SIGTSTP or SIGSTOP?
                stat = kill(pid, SIGSTOP);
            }
            if (stat == -1 || pidExists == 0) {
                sstring_destroy(s);
                vector_destroy(split);
                print_no_process_found(pid);
                return 0;
            } else {
                char c[250];
                size_t i = 0;
                while (i < vector_size(processVec)) {
                    process *p = *(process**)vector_at(processVec, i);
                    if (p->pid == pid) {
                        strcpy(c, p->command);
                        break;
                    } 
                    i++;
                }
                print_stopped_process(pid, c);
                sstring_destroy(s);
                vector_destroy(split);
                return 1;
            }
        }
    } else if (strcmp("cont", *(char**)vector_at(split,0))==0) {
        //sig = 1;
       
        if (vector_size(split) < 2 || strcmp(*(char**)vector_at(split,1),"")==0) {
            print_invalid_command(command);
            sstring_destroy(s);
            vector_destroy(split);
            return 0;
        } else {
            //printf("vecc = %s\n", *(char**)vector_at(split, 1));
            if (digits(*(char**)vector_at(split,1))==-1) {
                print_invalid_command(command);
                sstring_destroy(s);
                vector_destroy(split);
                return 0;
            }
            pid_t pid = atoi(*(char**)vector_at(split,1));
            //printf("pid = %d\n", pid);
            int stat = -1; //kill(pid, SIGCONT);

            int pidExists = 0;
            size_t i = 0;
            while (i < vector_size(processVec)) {
                process *p = *(process**)vector_at(processVec, i);
                if (p->pid == pid) {
                    char fn[256];
                    sprintf(fn, "/proc/%d/stat", p->pid);
                    FILE *stat = fopen(fn, "r");
                    if (stat == NULL) {
                        pidExists = 0;
                        // remove from processVec
                        vector_erase(processVec, i);
                        //fclose(stat);
                        break;
                    } else {
                        pidExists = 1;
                        fclose(stat);
                        break;
                    }
                }
                i++;
            }

            if (pidExists==1) {
                stat = kill(pid, SIGCONT);
            }
            if (stat == -1 || pidExists == 0) {
                sstring_destroy(s);
                vector_destroy(split);
                print_no_process_found(pid);
                return 0;
            } else {
                char c[250];
                size_t i = 0;
                while (i < vector_size(processVec)) {
                    process *p = *(process**)vector_at(processVec, i);
                    if (p->pid == pid) {
                        strcpy(c, p->command);
                        break;
                    } 
                    i++;
                }
                print_continued_process(pid, c);
                sstring_destroy(s);
                vector_destroy(split);
                return 1;
            }
        }
    }

    if (strcmp(command, "ps") == 0) {
        print_process_info_header();

        unsigned long long boot=0;
        FILE* btime=fopen("/proc/stat","r");
        char* line=NULL;
        size_t linecap=0;
        while(getline(&line, &linecap, btime)!=-1) {
            if(sizeof(line)>6) {
                line[5]='\0';
                if(strcmp(line,"btime")==0) {
                    boot=atoll(&line[6]);
                    //printf("boot = %llu\n", boot);
                    free(line);
                    break;
                }
            }
            //free(line);
        }
        //free(line);
        fclose(btime);

        process_info info;
        size_t i = 0;
        //process *p = malloc(sizeof(process));
        //p->command = strdup(command);
        //p->pid = getpid();
        //vector_push_back(processVec, p);
        size_t pSize = vector_size(processVec);
        //free(p->command);
        //free(p); 
        while (i < pSize) {
            process* p = *(process**)vector_at(processVec, i);
            //printf("LOOP = %s\n", p->command);
            //printf("LOOP PID = %d\n", p->pid);
            char fn[256];
            sprintf(fn, "/proc/%d/stat", p->pid);
            FILE *stat = fopen(fn, "r");
            if (stat == NULL) {
                i++;
                //printf("CONT");
                continue;
            }

            char *sinfo = NULL;
            size_t scapacity = 0;

            getline(&sinfo, &scapacity, stat);

            sstring *statstr = cstr_to_sstring(sinfo);
            vector *statVec = sstring_split(statstr, ' ');

            //for (size_t i = 0; i < vector_size(statVec); i++) {
            //    printf("STATVEC = %s\n", *(char**)vector_at(statVec, i));
            //}

            info.state = **(char**)vector_at(statVec, 2);
            //printf("infostate = %c\n", info.state);
            long int num_threads = 0;
            sscanf(*(char**)vector_at(statVec, 19), "%ld", &num_threads);
            info.nthreads = num_threads;
        
            unsigned long int vsize = 0;
            sscanf(*(char**)vector_at(statVec, 22),"%lu", &vsize);
            //printf("vsize = %lu\n", vsize);
            info.vsize = vsize/1024;

            unsigned long long start = 0;
            sscanf(*(char **)vector_at(statVec, 21), "%llu", &start);
            //printf("start = %llu\n", start);
            time_t epoch = ((start) / sysconf(_SC_CLK_TCK))+boot;

            struct tm *tinfo = localtime(&epoch);
            info.start_str = malloc(256);
            time_struct_to_string(info.start_str, 256, tinfo);

            unsigned long utime = 0;
            unsigned long stime = 0;
            //printf("index 13 = %s\n", *(char**)vector_at(statVec, 13));
            sscanf(*(char**)vector_at(statVec, 13), "%lu", &utime);
            sscanf(*(char**)vector_at(statVec, 14), "%lu", &stime);

            //unsigned long y = utime+stime;
            //printf("utime = %lu, stime = %lu\n", utime, stime);
            unsigned long y = (utime+stime)/sysconf(_SC_CLK_TCK);
            //printf("y = %lu\n", y);
            size_t min = (size_t)y / 60;
            size_t sec = (size_t)y % 60;
            //printf("min = %lu, sec = %lu\n", min, sec);
            info.time_str = malloc(256);

            execution_time_to_string(info.time_str, 256, min, sec);
            info.command = strdup(p->command);
            info.pid = p->pid;
            print_process_info(&info);
            free(info.start_str);
            free(info.time_str);
            free(info.command);
            free(sinfo);
           // free(tinfo);
            fclose(stat);
            sstring_destroy(statstr);
            vector_destroy(statVec); 
            i++;
        }
        sstring_destroy(s);
        vector_destroy(split);
        return 1; 
    }
    else if (strcmp(command, "cd") == 0) {
        print_invalid_command(command);
        sstring_destroy(s);
        vector_destroy(split);
        return 0;
    }
    else if (strcmp(*(char**)vector_at(split, 0), "cd") == 0) {
        char *directory = *(char**)vector_at(split,1);
        //printf("directory = %s\n", directory);
       if (vector_size(split) != 2) {
            //printf("HERE = %s\n", command);
            print_invalid_command(command);
            sstring_destroy(s);
            vector_destroy(split);
            return 0;
        } else {
            int d = chdir(directory);
            //printf("d = %d", d);
            if (d == -1) {
                print_no_directory(directory);
                sstring_destroy(s);
                vector_destroy(split);
                return 0;
            }
        }
        sstring_destroy(s);
        vector_destroy(split);
        return 1;
       
    } else {
        int status = parse_external_command_logic(command);

        sstring_destroy(s);
        vector_destroy(split);
        return status;
    }
    sstring_destroy(s);
    vector_destroy(split);
    return 0;
}

void parseLogic(char *command, int logic, vector *split) {
    if (logic == 0) return;
    
    if (logic == 1) {
        // and
        char cmd1[256];
        char cmd2[256];
        
        cmd1[0] = '\0';
        cmd2[0] = '\0';
        int seenLogic = 0;
        size_t splitSize = vector_size(split);
        for (size_t i = 0; i < splitSize; i++) {
            char *v = *(char**)vector_at(split, i);
            //printf("v = %s\n", v);
            if (seenLogic == 0 && strcmp(v, "&&")!=0) {
                strcat(cmd1, v);
                strcat(cmd1, " ");
            } else if (seenLogic == 1) {
                //printf("V = %d\n", seenLogic);
                strcat(cmd2, v);
                strcat(cmd2, " ");
            }
    
            //printf("cmd1 = %s\n", cmd1);
    
            if (strcmp(v, "&&")==0) {
                //printf("HERE\n");
                seenLogic = 1;
            }
        }
       
        size_t cmdSize = strlen(cmd1);
        cmd1[cmdSize-1] = '\0';

        size_t cmd2Size = strlen(cmd2);
        cmd2[cmd2Size-1] = '\0'; 
        int status = parse_command_no_history(cmd1);
        if (status == 1) {
            // run second command
            parse_command_no_history(cmd2);
        } else {
            return;
        }
    } else if (logic == 2) {
        // or
        char cmd1[256];
        char cmd2[256];

        cmd1[0] = '\0';
        cmd2[0] = '\0';
        int seenLogic = 0;
        size_t splitSize = vector_size(split); 
        for (size_t i = 0; i < splitSize; i++) {
            char *v = *(char**)vector_at(split, i);
            //printf("v = %s\n", v);
            if (seenLogic == 0 && strcmp(v, "||")!=0) {
                strcat(cmd1, v);
                strcat(cmd1, " "); 
            } else if (seenLogic == 1) {
                strcat(cmd2, v);
                strcat(cmd2, " ");
            }

            //printf("cmd1 = %s\n", cmd1);

            if (strcmp(v, "||")==0) {
                seenLogic = 1;
            }
        }

        size_t cmdSize = strlen(cmd1);
        cmd1[cmdSize-1] = '\0';

        size_t cmd2Size = strlen(cmd2);
        cmd2[cmd2Size-1] = '\0';

        int status = parse_command_no_history(cmd1);
        if (status == 1) {
            return;
        } else {
            // run second command
            parse_command_no_history(cmd2);
        }
    } else if (logic == 3) {
        // ;
        char cmd1[256];
        char cmd2[256];
        
        cmd1[0] = '\0';
        cmd2[0] = '\0';
        int seenLogic = 0;
        size_t splitSize = vector_size(split); 
        for (size_t i = 0; i < splitSize; i++) {
            char *v = *(char**)vector_at(split, i);
            //printf("v = %s\n", v);
            if (seenLogic == 0) {
                size_t vsize = strlen(v);
                if (v[vsize-1]==';') {
                    seenLogic = 1;
                    v[vsize-1] = '\0';
                }
                strcat(cmd1, v); 
                strcat(cmd1, " ");
            } else if (seenLogic == 1) {
                strcat(cmd2, v);
                strcat(cmd2, " ");
            }
        }

        size_t cmdSize = strlen(cmd1);
        cmd1[cmdSize-1] = '\0';

        size_t cmd2Size = strlen(cmd2);
        cmd2[cmd2Size-1] = '\0';

        parse_command_no_history(cmd1);
        parse_command_no_history(cmd2);

    } else if (logic == 4) {
        char cmd1[256];
        char cmd2[256];
        
        cmd1[0] = '\0';
        cmd2[0] = '\0';
        int seenLogic = 0;
        size_t splitSize = vector_size(split);
        for (size_t i = 0; i < splitSize; i++) {
            char *v = *(char**)vector_at(split, i);
            //printf("v = %s\n", v);
            if (seenLogic == 0 && strcmp(v, ">")!=0) {
                strcat(cmd1, v);
                strcat(cmd1, " ");
            } else if (seenLogic == 1) {
                //printf("V = %d\n", seenLogic);
                strcat(cmd2, v);
                strcat(cmd2, " ");
            }
    
            //printf("cmd1 = %s\n", cmd1);
    
            if (strcmp(v, ">")==0) {
                //printf("HERE\n");
                seenLogic = 1;
            }
        }
       
        size_t cmdSize = strlen(cmd1);
        cmd1[cmdSize-1] = '\0';

        size_t cmd2Size = strlen(cmd2);
        cmd2[cmd2Size-1] = '\0'; 
        fname = fopen(cmd2, "w+");
        if (fname == NULL) {
            print_redirection_file_error();
            return;
        }
        dupf = 1;
        parse_command_no_history(cmd1);
        fclose(fname);
    } else if (logic == 5) {
        char cmd1[256];
        char cmd2[256];
        
        cmd1[0] = '\0';
        cmd2[0] = '\0';
        int seenLogic = 0;
        size_t splitSize = vector_size(split);
        for (size_t i = 0; i < splitSize; i++) {
            char *v = *(char**)vector_at(split, i);
            //printf("v = %s\n", v);
            if (seenLogic == 0 && strcmp(v, ">>")!=0) {
                strcat(cmd1, v);
                strcat(cmd1, " ");
            } else if (seenLogic == 1) {
                //printf("V = %d\n", seenLogic);
                strcat(cmd2, v);
                strcat(cmd2, " ");
            }
    
            //printf("cmd1 = %s\n", cmd1);
    
            if (strcmp(v, ">>")==0) {
                //printf("HERE\n");
                seenLogic = 1;
            }
        }
       
        size_t cmdSize = strlen(cmd1);
        cmd1[cmdSize-1] = '\0';

        size_t cmd2Size = strlen(cmd2);
        cmd2[cmd2Size-1] = '\0'; 
        fname = fopen(cmd2, "a+");
        if (fname == NULL) {
            print_redirection_file_error();
            return;
        }
        dupf = 1;
        parse_command_no_history(cmd1);
        fclose(fname);
    } else if (logic == 6) {
        char cmd1[256];
        char cmd2[256];
        
        cmd1[0] = '\0';
        cmd2[0] = '\0';
        int seenLogic = 0;
        size_t splitSize = vector_size(split);
        for (size_t i = 0; i < splitSize; i++) {
            char *v = *(char**)vector_at(split, i);
            //printf("v = %s\n", v);
            if (seenLogic == 0 && strcmp(v, "<")!=0) {
                strcat(cmd1, v);
                strcat(cmd1, " ");
            } else if (seenLogic == 1) {
                //printf("V = %d\n", seenLogic);
                strcat(cmd2, v);
                strcat(cmd2, " ");
            }
    
            //printf("cmd1 = %s\n", cmd1);
    
            if (strcmp(v, "<")==0) {
                //printf("HERE\n");
                seenLogic = 1;
            }
        }
       
        size_t cmdSize = strlen(cmd1);
        cmd1[cmdSize-1] = '\0';

        size_t cmd2Size = strlen(cmd2);
        cmd2[cmd2Size-1] = '\0'; 
        fname = fopen(cmd2, "r");
        if (fname == NULL) {
            print_redirection_file_error();
            return;
        }
        dupf = 2;
        parse_command_no_history(cmd1);
        fclose(fname);
    }
    //fclose(fname);
}
 
void parse_command(char *command, vector *historyCmds) {
    // write into history file
    // implement cd
    //printf("command = %s\n", command);
    int logic = 0;  // logic = 0: no logic, 1: &&, 2: &, 3: ;, 4: >, 5: >>, 6: <

    sstring *s = cstr_to_sstring(command);
    vector *split = sstring_split(s, ' ');
    
    size_t splitSize = vector_size(split);
    
    if (splitSize == 0 || strlen(command)==0) {
        vector_destroy(split);
        sstring_destroy(s);
        return;
    }
    for (size_t i = 0; i < splitSize; i++) {
        char *op = *(char**)vector_at(split, i);
        size_t vsize = strlen(op);
        if (vsize==0) continue;
        if (strcmp(op, "&&")==0) {
            logic = 1;
        } else if (strcmp(op, "||")==0) {
            logic = 2;
        } else if (op[vsize-1] == ';') {
            logic = 3;
        } else if (strcmp(op, ">")==0) {
            logic = 4;
        } else if (strcmp(op, ">>")==0) {
            logic = 5;
        } else if (strcmp(op, "<")==0) {
            logic = 6;
        }
    }

    // signals
    int sig = 0;
    if (logic == 0 && strcmp("kill", *(char**)vector_at(split,0))==0) {
        //printf("AT KILL\n");
        sig = 1;
       
        if (vector_size(split) < 2 || strcmp(*(char**)vector_at(split,1),"")==0) {
            print_invalid_command(command);
        } else {
            //printf("vecc = %s\n", *(char**)vector_at(split, 1));
            if (digits(*(char**)vector_at(split,1))==-1) {
                print_invalid_command(command);
            } else {

                pid_t pid = atoi(*(char**)vector_at(split,1));
                //printf("pid = %d\n", pid);
                int pidExists = 0;
                size_t i = 0;
                while (i < vector_size(processVec)) {
                    process *p = *(process**)vector_at(processVec, i); 
                    if (p->pid == pid) {
                        char fn[256];
                        sprintf(fn, "/proc/%d/stat", p->pid);
                        FILE *stat = fopen(fn, "r");
                        if (stat == NULL) {
                            pidExists = 0;
                            // remove from processVec
                            vector_erase(processVec, i);
                            //fclose(stat);
                            break;
                        } else {
                            pidExists = 1;
                            fclose(stat);
                            break;
                        }

                    }
                    i++;
                }
                int stat = -1;
                if (pidExists == 1)
                    stat = kill(pid, SIGTERM);
                if (stat == -1 || pidExists == 0) {
                    print_no_process_found(pid);
                } else {
                    char c[250];
                    size_t i = 0;
                    while (i < vector_size(processVec)) {
                        process *p = *(process**)vector_at(processVec, i);
                        if (p->pid == pid) {
                            strcpy(c, p->command);
                            break;
                        } 
                        i++;
                    }
                    vector_erase(processVec, i);
                    print_killed_process(pid, c);
                }
            }
        }
    } else if (logic == 0 && strcmp("stop", *(char**)vector_at(split, 0))==0) {
         sig = 1;
       
        if (vector_size(split) < 2 || strcmp(*(char**)vector_at(split,1),"")==0) {
            print_invalid_command(command);
        } else {
            //printf("vecc = %s\n", *(char**)vector_at(split, 1));
            if (digits(*(char**)vector_at(split,1))==-1) {
                print_invalid_command(command);
            } else {

                pid_t pid = atoi(*(char**)vector_at(split,1));
                //printf("pid = %d\n", pid);
                int stat = -1;
                int pidExists = 0;
                size_t i = 0;
                while (i < vector_size(processVec)) {
                    process *p = *(process**)vector_at(processVec, i); 
                    if (p->pid == pid) {
                        char fn[256];
                        sprintf(fn, "/proc/%d/stat", p->pid);
                        FILE *stat = fopen(fn, "r");
                        if (stat == NULL) {
                            pidExists = 0;
                            // remove from processVec
                            vector_erase(processVec, i);
                            //fclose(stat);
                            break;
                        } else {
                            pidExists = 1;
                            fclose(stat);
                            break;
                        }

                    }
                    i++;
                }
                if (pidExists == 1) {
                    // TODO: SIGSTOP or SIGTSTP?
                    stat = kill(pid, SIGSTOP);
                }
                if (stat == -1 || pidExists == 0) {
                    print_no_process_found(pid);
                } else {
                    char c[250];
                    size_t i = 0;
                    while (i < vector_size(processVec)) {
                        process *p = *(process**)vector_at(processVec, i);
                        if (p->pid == pid) {
                            strcpy(c, p->command);
                            break;
                        } 
                        i++;
                    }
                    print_stopped_process(pid, c);
                }
            }
        }
    } else if (logic == 0 && strcmp("cont", *(char**)vector_at(split,0))==0) {
        sig = 1;
       
        if (vector_size(split) < 2 || strcmp(*(char**)vector_at(split,1),"")==0) {
            print_invalid_command(command);
        } else {
            //printf("vecc = %s\n", *(char**)vector_at(split, 1));
            if (digits(*(char**)vector_at(split,1))==-1) {
                print_invalid_command(command);
            } else {

                pid_t pid = atoi(*(char**)vector_at(split,1));
                //printf("pid = %d\n", pid);
                int stat = -1; //kill(pid, SIGCONT);

                int pidExists = 0;
                size_t i = 0;
                while (i < vector_size(processVec)) {
                    process *p = *(process**)vector_at(processVec, i);
                    if (p->pid == pid) {
                        char fn[256];
                        sprintf(fn, "/proc/%d/stat", p->pid);
                        FILE *stat = fopen(fn, "r");
                        if (stat == NULL) {
                            pidExists = 0;
                            // remove from processVec
                            vector_erase(processVec, i);
                            //fclose(stat);
                            break;
                        } else {
                            pidExists = 1;
                            fclose(stat);
                            break;
                        }

                    }
                    i++;
                }

                if (pidExists==1) {
                    stat = kill(pid, SIGCONT);
                }
                if (stat == -1 || pidExists == 0) {
                    print_no_process_found(pid);
                } else {
                    char c[250];
                    size_t i = 0;
                    while (i < vector_size(processVec)) {
                        process *p = *(process**)vector_at(processVec, i);
                        if (p->pid == pid) {
                            strcpy(c, p->command);
                            break;
                        } 
                        i++;
                    }
                    print_continued_process(pid, c);
                }
            }   
        }
    }
    int cd = 0;
    int ps = 0;
    //printf("logic == %d\n", logic);
    if (strcmp(command, "ps") == 0 && logic == 0) {
        // print process info
        ps = 1;
        print_process_info_header();

        unsigned long long boot=0;
        FILE* btime=fopen("/proc/stat","r");
        char* line=NULL;
        size_t linecap=0;
        while(getline(&line, &linecap, btime)!=-1) {
            if(sizeof(line)>6) {
                line[5]='\0';
                if(strcmp(line,"btime")==0) {
                    boot=atoll(&line[6]);
                    //printf("boot = %llu\n", boot);
                    free(line);
                    break;
                }
            }
            //free(line);
        }
        //free(line);
        fclose(btime);

        process_info info;
        size_t i = 0;
        //process *p = malloc(sizeof(process));
        //p->command = strdup(command);
        //p->pid = getpid();
        //vector_push_back(processVec, p);
        size_t pSize = vector_size(processVec);
        //free(p->command);
        //free(p); 
        while (i < pSize) {
            process* p = *(process**)vector_at(processVec, i);
            //printf("LOOP = %s\n", p->command);
            //printf("LOOP PID = %d\n", p->pid);
            char fn[256];
            sprintf(fn, "/proc/%d/stat", p->pid);
            FILE *stat = fopen(fn, "r");
            if (stat == NULL) {
                i++;
                //printf("CONT");
                continue;
            }

            char *sinfo = NULL;
            size_t scapacity = 0;

            getline(&sinfo, &scapacity, stat);

            sstring *statstr = cstr_to_sstring(sinfo);
            vector *statVec = sstring_split(statstr, ' ');

            //for (size_t i = 0; i < vector_size(statVec); i++) {
            //    printf("STATVEC = %s\n", *(char**)vector_at(statVec, i));
            //}

            info.state = **(char**)vector_at(statVec, 2);
            //printf("infostate = %c\n", info.state);
            long int num_threads = 0;
            sscanf(*(char**)vector_at(statVec, 19), "%ld", &num_threads);
            info.nthreads = num_threads;
        
            unsigned long int vsize = 0;
            sscanf(*(char**)vector_at(statVec, 22),"%lu", &vsize);
            //printf("vsize = %lu\n", vsize);
            info.vsize = vsize/1024;

            unsigned long long start = 0;
            sscanf(*(char **)vector_at(statVec, 21), "%llu", &start);
            //printf("start = %llu\n", start);
            time_t epoch = ((start) / sysconf(_SC_CLK_TCK))+boot;

            struct tm *tinfo = localtime(&epoch);
            info.start_str = malloc(256);
            time_struct_to_string(info.start_str, 256, tinfo);

            unsigned long utime = 0;
            unsigned long stime = 0;
            //printf("index 13 = %s\n", *(char**)vector_at(statVec, 13));
            sscanf(*(char**)vector_at(statVec, 13), "%lu", &utime);
            sscanf(*(char**)vector_at(statVec, 14), "%lu", &stime);

            //unsigned long y = utime+stime;
            //printf("utime = %lu, stime = %lu\n", utime, stime);
            unsigned long y = (utime+stime)/sysconf(_SC_CLK_TCK);
            //printf("y = %lu\n", y);
            size_t min = (size_t)y / 60;
            size_t sec = (size_t)y % 60;
            //printf("min = %lu, sec = %lu\n", min, sec);
            info.time_str = malloc(256);

            execution_time_to_string(info.time_str, 256, min, sec);
            info.command = strdup(p->command);
            info.pid = p->pid;
            print_process_info(&info);
            free(info.start_str);
            free(info.time_str);
            free(info.command);
            free(sinfo);
           // free(tinfo);
            fclose(stat);
            sstring_destroy(statstr);
            vector_destroy(statVec); 
            i++;
        } 
    }

    if (strcmp(command, "cd") == 0 && logic == 0) {
        print_invalid_command(command);
        cd = 1;
    }
    else if (strcmp(*(char**)vector_at(split, 0), "cd") == 0 && logic == 0) {
        char *directory = *(char**)vector_at(split,1);
        //printf("directory = %s\n", directory);
        cd = 1;
       if (vector_size(split) != 2) {
            print_invalid_command(command);
        } else {
            int d = chdir(directory);
            //printf("d = %d", d);
            if (d == -1) {
                print_no_directory(directory);
            }
        }
    }

    sstring_destroy(s);
    vector_destroy(split);
   
    if (strcmp(command, "!history")==0) {
        // print everything from history in order
       for(size_t i=0;i<vector_size(historyCmds);i++){
            print_history_line(i, vector_get(historyCmds,i));
        }
        return;
    }

    if (command[0] == '#' && logic == 0) {
        if (strlen(command) == 1) {
            print_invalid_command(command);
            return;
        } else {
            char* num = command+1;
        
            size_t size = strlen(num);
        
            for (size_t i = 0; i < size; i++) {
                if (isdigit(num[i])==0) {
                    print_invalid_index();
                    return;
                }
            }
            int number = atoi(num);

            if ((size_t)number >= vector_size(historyCmds)) {
                print_invalid_index();
                return;
            } else {
                char *newCommand = *(char**)vector_at(historyCmds, number);
                print_command(newCommand);
                parse_command(newCommand, historyCmds);
                return; 
            }
        }
    } else if (command[0] == '!' && logic == 0) {
        size_t historySize = vector_size(historyCmds);
        char *prefix = command+1;
        size_t prefixSize = strlen(prefix);
        int b = 0;
        char *hist = NULL;

        for (int i = (int)historySize-1; i >= 0; i--) {
            hist = *(char**)vector_at(historyCmds, i);
            if (strncmp(prefix, hist, prefixSize) == 0) {
                //print_command(hist);
                b = 1;
                break;
            }
        }

        if (b == 0) {
            print_no_history_match();
        } else {
            print_command(hist);
            parse_command(hist, historyCmds);
        }
        return;    
    } else if (logic == 0 && sig == 0 && cd == 0 && ps == 0) {
        parse_external_command(command);
    }

    if (logic > 0) {
        sstring *m = cstr_to_sstring(command);
        vector *spl = sstring_split(m, ' ');
        parseLogic(command, logic, spl);
        vector_destroy(spl);
        sstring_destroy(m);
    }
    
    if (history_file_name != NULL) {
        //printf("HISTORY COMMAND = %s\n", command);
        fputs(command, history_file_name);
        fputs("\n", history_file_name);
        vector_push_back(historyCmds,command);
    } else {
        vector_push_back(historyCmds, command);
    }
}

void sigIntHandler(int signal) {
    if (parentpid != 0 && childpid != 0) {
        kill(childpid, SIGINT);
    }
    return;
}

void sigChildHandler() {
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0){}
    return;
}

int shell(int argc, char *argv[]) {
    // TODO: This is the entry point for your shell.
    signal(SIGINT, sigIntHandler);
    //signal(SIGCHLD, sigChildHandler);
    FILE *file_name = NULL;
    stdoutCopy = dup(1);
    stdinCopy = dup(0);

    vector *historyCmds = vector_create(string_copy_constructor, string_destructor, string_default_constructor);
    if (argc != 1 && argc != 3 && argc != 5) {
        print_usage();
        vector_destroy(historyCmds);
        //vector_destroy(processVec);
        exit(0);
    }

    char opt = 0;
    // stdin, stdout, and stderr are 0, 1, and 2, respectively.
    int stream = 0;
    //vector *historyVec = vector_create(string_copy_constructor, string_destructor, string_default_constructor);

    while (opt != -1) {
        opt = getopt(argc, argv, "h:f:");
        
        if (opt == 'h') {
            // get history file
            if (access(optarg, F_OK) != -1) {
                char *full_path = get_full_path(optarg);
                history_file_name = fopen(full_path, "a+");
                free(full_path);

                if (history_file_name == NULL) {
                    print_history_file_error();
                }
                char *historyline = NULL;
                size_t n = 0;
                ssize_t size = getline(&historyline, &n, history_file_name);
        
                while (size != -1) {
            
                    //historyline = (char *)realloc(historyline,size+1);
                    if (historyline[size-1] == '\n') {
                        historyline[size-1] = '\0';
                    } else {
                        historyline = (char *)realloc(historyline, size+1);
                        historyline[size] = '\0';
                    }
                    vector_push_back(historyCmds, historyline); 
                    size = getline(&historyline, &n, history_file_name);
                    //free(historyline);
                }
                free(historyline);

            } else {
                history_file_name = fopen(optarg, "w+");
            }
        } else if (opt == 'f') {
            char *fp = get_full_path(optarg);
            file_name = fopen(fp, "r");
            free(fp);
            if (file_name != NULL) {
                stream = 3;    
            } else {
                print_script_file_error();
                vector_destroy(historyCmds);
                if (history_file_name != NULL)
                    fclose(history_file_name);
                if (file_name != NULL)
                    fclose(file_name);
                exit(0);
            }
        }
    }
    processVec = vector_create(process_copy_constructor, process_destructor, process_default_constructor);
    process *p = malloc(sizeof(process));
    p->pid = getpid();
    p->command = strdup(argv[0]);
    vector_push_back(processVec, p);
    free(p->command);
    free(p);
    if (file_name != NULL) {

        // for each command, parse command
        // getline -- char **lineptr, size_t *n, FILE *stream
        char *fileline = NULL;
        size_t fn = 0;
        ssize_t fsize = getline(&fileline, &fn, file_name);

        while (fsize != -1) {
            //fileline = (char *)realloc(fileline,fsize+1);
            if (fileline != NULL && fileline[fsize-1] == '\n') {
                fileline[fsize-1] = '\0';
            } else {
                fileline = (char*)realloc(fileline, fsize+1);
                fileline[fsize] = '\0';
            }
            print_command(fileline);
            parse_command(fileline, historyCmds);
            //free(fileline);
            fsize = getline(&fileline, &fn, file_name);
        }
        
        free(fileline);

        vector_destroy(historyCmds);
        if (history_file_name != NULL)
            fclose(history_file_name);
        if (file_name != NULL)
            fclose(file_name);
        vector_destroy(processVec);
        exit(0);
        //free(&fileline);
    }

    int ssize = 2048;
    char *currentDir = (char*)malloc(ssize);
    char *input = (char*)malloc(ssize);
    while (1) {
        currentDir = getcwd(currentDir, ssize);
        if (currentDir) {
            print_prompt(currentDir, getpid());
            fflush(stdout);
        }
        
        char *firstNewLine = NULL;
        if (stream == 0 || stream == 3) {
            // stdin
            *input = 0;
            char *inputNum = fgets(input, ssize, stdin);
            int status = 0;
            while (waitpid(-1, &status, WNOHANG) > 0){}
            if (*input == EOF) {
                //vector_destroy(historyVec);
                vector_destroy(historyCmds);
                if (history_file_name != NULL)
                    fclose(history_file_name);
                if (file_name != NULL)
                    fclose(file_name);
                free(input);
                free(currentDir);
                vector_destroy(processVec);
                exit(0);
            }

            if (inputNum != 0) {
                firstNewLine = strchr(input, '\n');
                if (firstNewLine != NULL) {
                    *(firstNewLine) = '\0';
                }
                if (strcmp(input, "exit") == 0) {
                    vector_destroy(historyCmds);
                    //vector_destroy(historyVec);
                    if (history_file_name != NULL)
                        fclose(history_file_name);
                    if (file_name != NULL)
                        fclose(file_name);
                    free(input);
                    free(currentDir);
                    //free(inputNum);
                    //free(firstNewLine);
                    vector_destroy(processVec);
                    exit(0);
                }
                parse_command(input, historyCmds);
            } else {
                //vector_destroy(historyVec);
                vector_destroy(historyCmds);
                if (history_file_name != NULL)
                    fclose(history_file_name);
                if (file_name != NULL)
                    fclose(file_name);
                free(input);
                free(currentDir);
                vector_destroy(processVec);
                exit(0);
            }
        }
        
    }
    
    return 0;
}
