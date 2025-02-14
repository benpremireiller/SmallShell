#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h> // for waitpid
#include <signal.h> // FOr signal codes

#define INPUT_LENGTH 2048
#define MAX_ARGS 512
#define MAX_BG_COMMANDS 50


struct command_line
{
    char *command;
    char *argv[MAX_ARGS + 1];
    int argc;
    char *input_file;
    char *output_file;
    bool is_bg;
    bool is_comment;
};

void print_command_line_struct(struct command_line* cl){

    printf("Command: %sInput File: %s\nOutput File: %s\nBackground: %d\n", cl->command, cl->input_file, cl->output_file, cl->is_bg);
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
        curr_command->is_comment = true;
        return curr_command;
    }

    curr_command->command = token;
    
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

    // Uses the first argument as the directory to change to
    if (cl->argc == 0){
        chdir("/home");
    } else {
        chdir(cl->argv[0]);
    }

}

void print_status(int status){

    printf("exit value %d", status);
}


void cleanup_exit(int bg_processes[]){

    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        kill(bg_processes[i], SIGKILL) // No need to worry if the process is already complete and in zombie state

    }

}


void reap_finished_bg_processes(int bg_processes[]){

    int childExitStatus;
    int bg_pid;

    // Check each process to see if they are complete and ready to be reaped
    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];

        if (bg_pid != NULL){ // a PID
            bg_pid  = waitpid(bg_pid , &childExitStatus, WNOHANG);

            if (bg_pid > 0){ // Process complete
                printf("background pid %d is done: exit value %d", bg_pid, childExitStatus);
                bg_processes[i] = NULL;

            }
        }
    }

}

void insert_into_bg_process_array(int bg_processes[], int pid){

    int bg_pid;

    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];
        if (bg_pid == NULL){
            bg_processes[i] = pid;
            break;
        }
    }

}

int redirect_stdin(char *source){

    int sourceFD = open(source, O_RDONLY);
    if (sourceFD == -1){
        printf("cannot open %s for input", source);
        fflush(stdout);
        return -1;
    }

    dup2(sourceFD, stdin);
    return 0;

}

int redirect_stdin(char *destination){

    int destinationFD = open(destination, O_WRONLY);
    if (destinationFD == -1){
        printf("cannot open %s for output", destination);
        fflush(stdout);
        return -1;
    }
    dup2(destinationFD, stdout);
    return 0;

}

int main(){

    int status = 0;
    int spawnpid;
    int childExitStatus;
    struct command_line *curr_command;
    int destinationFD;
    int sourceFD;
    int bg_processes[MAX_BG_COMMANDS];
    int bg_process_cnt;

    while(true)
    {
        curr_command = parse_input();
        //print_command_line_struct(curr_command);

        if (curr_command->is_comment){
            continue;

        } else if (!strcmp(curr_command->command, "cd")){
            cd(curr_command);

        } else if (!strcmp(curr_command->command, "status")){
            print_status(status);

        } else if (!strcmp(curr_command->command, "exit")){
            cleanup_exit(bg_processes);
            return EXIT_SUCCESS;
            
        } else {

            spawnpid = fork();
            switch (spawnpid){
                case -1:
                    perror("fork() failed!");
                    exit(1);

                case 0: // Child
                    
                    // Redirect streams if necessary. Exit on failure.
                    if(curr_command->is_bg && !curr_command->input_file && redirect_stdin("/dev/null") == -1){
                        exit(2);
                    }
                    
                    if(curr_command->input_file && redirect_stdin(curr_command->input_file) == -1){
                        exit(2);
                    }

                    if(curr_command->is_bg && !curr_command->output_file && redirect_stdout("/dev/null") == -1){
                        exit(2);
                    }

                    if(curr_command->output_file && redirect_stdout(curr_command->output_file) == -1){
                        exit(2);
                    }

                    
                    // Run command with exec
                    (curr_command->argv)[curr_command->argc+1] = NULL; // Add null terminator
                    execvp(curr_command->command, curr_command->argv);
                    perror("execvp"); // Only get here if exec fails
                    exit(3);
        
                default: // Parent
                    
                    if(!curr_command->is_bg){
                        spawnpid = waitpid(spawnpid, &childExitStatus, 0);
                        if (WIFEXITED(childExitStatus)){
                            status = 1; 
                        }

                    } else {
                        insert_into_bg_process_array(bg_processes, spawnpid);

                    }
                    break;
            }
        }

        // Check for finished background commands
        reap_finished_bg_processes(bg_processes);

    }

    return EXIT_SUCCESS;
}
