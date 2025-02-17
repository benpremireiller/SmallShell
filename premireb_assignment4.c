#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h> // for waitpid
#include <signal.h> // FOr signal codes

#define INPUT_LENGTH 2048
#define MAX_ARGS 512
#define MAX_BG_COMMANDS 50

bool delay_SIGTSTP = false;
bool pending_SIGSTP = false;
bool ignore_bg = false;



struct command_line
{
    char *command;
    char *argv[MAX_ARGS + 1];
    int argc;
    char *input_file;
    char *output_file;
    bool is_bg;
    bool ignore;
};

void print_command_line_struct(struct command_line* cl){

    printf("Command: %s\nInput File: %s\nOutput File: %s\nBackground: %d\n", cl->command, cl->input_file, cl->output_file, cl->is_bg);
    fflush(stdout);
    for(int i = 0; i < cl->argc; i++){
        printf("Arg %i: %s\n", i, (cl->argv)[i]);
        fflush(stdout);
    }

}

struct command_line *parse_input(){

    char input[INPUT_LENGTH];
    struct command_line *curr_command = (struct command_line *) calloc(1,sizeof(struct command_line));
    // Get input
    printf(": ");
    fflush(stdout);

    fgets(input, INPUT_LENGTH, stdin);
    
    // Parse the command or comment separately
    char *token = strtok(input, " \n");

    if(token == NULL || !strcmp(token, "#")){ // blank or comment
        curr_command->ignore = true;
        return curr_command;
    }

    curr_command->ignore = false;
    curr_command->command = strdup(token);
    curr_command->argv[curr_command->argc++] = strdup(token);
    
    // Tokenize remaining args
    token = strtok(NULL, " \n");
    while(token){

        if(!strcmp(token,"<")){
            curr_command->input_file = strdup(strtok(NULL," \n"));

        } else if(!strcmp(token,">")){
            curr_command->output_file = strdup(strtok(NULL," \n"));

        } else if(!strcmp(token,"&")){
            curr_command->is_bg = true;

        } else{
            curr_command->argv[curr_command->argc++] = strdup(token);

        }

        token=strtok(NULL," \n");
    }

    return curr_command;
}

void cd(struct command_line* cl){

    // Uses the second argument as the directory to change to
    if (cl->argc == 0){
        chdir("/home");
    } else {
        chdir(cl->argv[1]);
    }

}

void print_status(int status){

    printf("exit value %d\n", status);
}


void cleanup_exit(int bg_processes[]){

    int bg_pid;

    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];

        if(bg_pid != -1){ 
            kill(bg_processes[i], SIGKILL); // No need to worry if the process is already complete and in zombie state
        }

    }

}

void reap_finished_bg_processes(int bg_processes[]){

    int childExitStatus;
    int bg_pid;

    // Check each process to see if they are complete and ready to be reaped
    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];

        if (bg_pid != -1){ // a PID
            bg_pid = waitpid(bg_pid , &childExitStatus, WNOHANG);
            
            if (WIFEXITED(childExitStatus)){
                printf("background pid %d is done: exit value %d\n", bg_pid, childExitStatus);
                fflush(stdout);
                bg_processes[i] = -1;

            } else if (WIFSIGNALED(childExitStatus)){
                printf("background pid %d is done: terminated by signal %d", bg_pid, WTERMSIG(childExitStatus));
                fflush(stdout);
                bg_processes[i] = -1;
        }
    }

}

void insert_into_bg_process_array(int bg_processes[], int pid){

    int bg_pid;

    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];
        if (bg_pid == -1){
            bg_processes[i] = pid;
            break;
        }
    }

}

int redirect_stdin(char *source){

    int sourceFD = open(source, O_RDONLY);
    if (sourceFD == -1){
        printf("cannot open %s for input\n", source);
        fflush(stdout);
        return -1;
    }

    dup2(sourceFD, 0);
    return 0;

}

int redirect_stdout(char *destination){

    int destinationFD = open(destination, O_WRONLY | O_CREAT | O_TRUNC);
    if (destinationFD == -1){
        printf("cannot open %s for output\n", destination);
        fflush(stdout);
        return -1;
    }
    dup2(destinationFD, 1);
    return 0;

}

void handle_SIGTSTP(int signo){ //Ctrl-Z
    
    if (delay_SIGTSTP){
        pending_SIGSTP = true;
        return;
    }

    if (!ignore_bg){
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 49);
        ignore_bg = true;

    } else {
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 29);
        ignore_bg = false;

    }

    pending_SIGSTP = false;
    return;
}

void handle_SIGINT(int signo){ //Ctrl-C
    exit(signo); // This right??
}


int main(){

    int status = 0;
    int spawnpid;
    int childExitStatus;
    struct command_line *curr_command;
    int destinationFD;
    int sourceFD;
    int bg_processes[MAX_BG_COMMANDS] = {-1}; // Change this type to pid_t?
    int bg_process_cnt;

    // Set up signal handlers
    struct sigaction SIGTSTP_action = {0}, ignore_action = {0};
    
    // Handle SIGTSTP
    SIGTSTP_action.sa_handler = handle_SIGTSTP; 
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    sigfillset(&SIGTSTP_action.sa_mask); // Block all signals while running

    // Ignore SIGINT
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &ignore_action, NULL);

    while(true)
    {
        
        // Delay SIGTSTP signal until after execution of command, may need to move these around fgets() in parse_input()
        delay_SIGTSTP = false;
        curr_command = parse_input();
        delay_SIGTSTP = true;

        //print_command_line_struct(curr_command);

        // Handle built-in command first
        if (curr_command->ignore){
            continue;

        } else if (!strcmp(curr_command->command, "cd")){
            cd(curr_command);

        } else if (!strcmp(curr_command->command, "status")){
            print_status(status);

        } else if (!strcmp(curr_command->command, "exit")){
            cleanup_exit(bg_processes);
            return EXIT_SUCCESS;
            
        } else { // Non built-in command. 
            spawnpid = fork();
            switch (spawnpid){

                case -1:
                    perror("fork() failed!");
                    exit(1);

                case 0: // Child
                    
                    // Ignore SIGTSTP for all children. 
                    sigaction(SIGTSTP, &ignore_action, NULL);

                    // Handle SIGINT
                    struct sigaction SIGINT_action = {0};
                    SIGINT_action.handler = handle_SIGINT;
                    sigaction(SIGINT, &SIGINT_action, NULL);

                    if (curr_command->is_bg && !ignore_bg){

                        // Background processes ignore SIGINT
                        sigaction(SIGINT, &ignore_action, NULL);

                        printf("background pid is %d\n", getpid());
                        fflush(stdout);

                        // Redirect bg process streams to /dev/null if there is no value specified 
                        if(!curr_command->input_file && redirect_stdin("/dev/null") == -1){
                            exit(2);
                        }

                        if(!curr_command->output_file && redirect_stdout("/dev/null") == -1){
                            exit(2);
                        }
                    }

                    // Redirect streams if necessary
                    if(curr_command->input_file && redirect_stdin(curr_command->input_file) == -1){
                        exit(2);
                    }

                    if(curr_command->output_file && redirect_stdout(curr_command->output_file) == -1){
                        exit(2);
                    }

                    // Run command with exec. Search PATH and execute it in child process.
                    (curr_command->argv)[(curr_command->argc)+1] = NULL; // Add null terminator to args
                    execvp(curr_command->command, curr_command->argv);
                    perror("execvp");
                    exit(3);
        
                default: // Parent
                    
                    if(!curr_command->is_bg || ignore_bg){
                        spawnpid = waitpid(spawnpid, &childExitStatus, 0);

                        if (WIFEXITED(childExitStatus)){
                            status = 0; // TODO, this is returning 0 even if I send a bogus command to execvp.

                        } else if (WIFSIGNALED(childExitStatus)){
                            printf("terminated by signal %d", WTERMSIG(childExitStatus));
                            fflush(stdout);

                        } else {
                            status = 1;

                        }

                    } else {
                        insert_into_bg_process_array(bg_processes, spawnpid);

                    }
                    break;
            }
        }

        // Send SIGTSTP to current process if pending
        if (pending_SIGSTP){
            kill(getpid(), SIGTSTP);
        }

        // Check for finished background commands
        reap_finished_bg_processes(bg_processes);

    }

    return EXIT_SUCCESS;
}
