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
#define MAX_BG_COMMANDS 5

bool ignore_bg = false;

// Define the struct we will use to store data about commands
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


// Define functions
void print_command_line_struct(struct command_line* cl);
struct command_line *parse_input();
void cd(struct command_line* cl);
void print_status(int status);
void cleanup_exit(int bg_processes[]);
void reap_finished_bg_processes(int bg_processes[]);
void insert_into_bg_process_array(int bg_processes[], int pid);
int redirect_stdin(char *source);
int redirect_stdout(char *destination);
void handle_SIGTSTP(int signo);
void handle_SIGINT(int signo);



int main(){

    int status = 0;
    int spawnpid;
    int childExitStatus;
    struct command_line *curr_command;
    int destinationFD;
    int sourceFD;
    int bg_processes[MAX_BG_COMMANDS] = {0}; // Change this type to pid_t?
    sigset_t blockSet, prevMask;
    struct sigaction SIGTSTP_action = {0}, ignore_action = {0};  // Set up signal handlers
    
    // Handle SIGTSTP
    SIGTSTP_action.sa_handler = handle_SIGTSTP; 
    //SIGTSTP_action.sa_flags = SA_RESTART;
    sigfillset(&SIGTSTP_action.sa_mask); // Block all signals while running handler
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Ignore SIGINT
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &ignore_action, NULL);

    // Create signal set for SIGTSTP mask
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGTSTP);

    while(true)
    {
        
        // Get command then delay SIGTSTP signal until after execution of command
        curr_command = parse_input();
        if (sigprocmask(SIG_BLOCK, &blockSet, &prevMask) == -1){
            return EXIT_FAILURE;
        }

        // Handle built-in command first in parent
        if (curr_command == NULL || curr_command->ignore){ // Error in read or blank line
            // Do nothing

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
                    sigaction(SIGTSTP, &ignore_action, NULL); // Reuse parent sigaction struct

                    // Handle SIGINT
                    struct sigaction SIGINT_action = {0};
                    SIGINT_action.sa_handler = handle_SIGINT;
                    sigaction(SIGINT, &SIGINT_action, NULL);

                    if (curr_command->is_bg && !ignore_bg){

                        // Background processes ignore SIGINT
                        sigaction(SIGINT, &ignore_action, NULL);

                        // Redirect bg process streams to /dev/null if there is no value specified 
                        if(!curr_command->input_file){
                            redirect_stdin("/dev/null"); // Should not fail
                        }

                        if(!curr_command->output_file){
                            redirect_stdout("/dev/null");
                        }
                    }

                    // Redirect streams if necessary
                    if(curr_command->input_file && redirect_stdin(curr_command->input_file) == -1){
                        exit(1);
                        // This needs to 
                    }

                    if(curr_command->output_file && redirect_stdout(curr_command->output_file) == -1){
                        exit(1);
                    }

                    // Run command with exec. Search PATH and execute it in child process.
                    (curr_command->argv)[(curr_command->argc)+1] = NULL; // Add null terminator to args
                    execvp(curr_command->command, curr_command->argv);
                    perror("execvp");
                    exit(2);
        
                default: // Parent
                    
                    if(!curr_command->is_bg || ignore_bg){
                        spawnpid = waitpid(spawnpid, &childExitStatus, 0);

                        if (WIFEXITED(childExitStatus)){

                            if (WEXITSTATUS(childExitStatus) == 1 || WEXITSTATUS(childExitStatus) == 2){
                                status = 1;
                            } else {
                                status = 0;
                            }

                        } else if (WIFSIGNALED(childExitStatus)){
                            printf("terminated by signal %d\n", WTERMSIG(childExitStatus));
                            fflush(stdout);

                        } else { // Other stop method (not handled here)
                            status = 1;

                        }

                    } else {
                        printf("background pid is %d\n", spawnpid);
                        fflush(stdout);
                        insert_into_bg_process_array(bg_processes, spawnpid);

                    }
                    break;
            }
        }

        // Unblock signals sent during process execution
        if (sigprocmask(SIG_SETMASK, &prevMask, NULL) == -1){
            return EXIT_FAILURE;
        }

        // Check for finished background commands
        reap_finished_bg_processes(bg_processes);

    }

    return EXIT_SUCCESS;
}


// Function which prints the contents of the command_line struct to stdout
// Arguments: A pointer to a command_line struct
// Returns: void
void print_command_line_struct(struct command_line* cl){

    printf("Argc: %d\nCommand: %s\nInput File: %s\nOutput File: %s\nBackground: %d\n", cl->argc, cl->command, cl->input_file, cl->output_file, cl->is_bg);
    fflush(stdout);
    for(int i = 0; i < cl->argc; i++){
        printf("Arg %i: %s\n", i, (cl->argv)[i]);
        fflush(stdout);
    }

}

// Function which parses input from the user and stores data in a command_line struct
// Arguments: void
// Returns: A command_line struct
struct command_line *parse_input(){

    char input[INPUT_LENGTH];
    struct command_line *curr_command = (struct command_line *) calloc(1,sizeof(struct command_line));

    // Get input
    printf(": ");
    fflush(stdout);

    if (fgets(input, INPUT_LENGTH, stdin) == NULL){
        return NULL;
    }

    // Parse the command or comment separately
    char *token = strtok(input, " \n");

    if(token == NULL || !strncmp(token, "#", 1)){ // blank or comment
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

// Command line function which changes the current working directory of the process
// Arguments: A pointer to a command_line struct
// Returns: void
void cd(struct command_line* cl){

    // Uses the second argument as the directory to change to
    if (cl->argc == 1){
        chdir(getenv("HOME"));
    } else {
        chdir(cl->argv[1]);
    }

}

// Command line function which prints the status of the most recent command 
// Arguments: The status of the most recent command
// Returns: void
void print_status(int status){

    printf("exit value %d\n", status);
}


// Command line function which kills all background processes to be used before exiting
// Arguments: A pointer to an array of integers corresponding to pids
// Returns: void
void cleanup_exit(int bg_processes[]){

    int bg_pid;

    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];

        if(bg_pid != 0){ 
            kill(bg_processes[i], SIGKILL); // No need to worry if the process is already complete and in zombie state
        }

    }

}

// Command line function which checks the state of background processes and reaps them if they are complete. 
// Arguments: A pointer to an array of integers corresponding to pids
// Returns: void
void reap_finished_bg_processes(int bg_processes[]){

    int childExitStatus;
    int bg_pid;

    // Check each process to see if they are complete and ready to be reaped
    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];

        if (bg_pid != 0){ // a process
            bg_pid = waitpid(bg_pid , &childExitStatus, WNOHANG);

            if (bg_pid == 0){ // pid exists but state has not changed
                continue;
            }
            
            // Check for termination reason and print info
            if (WIFEXITED(childExitStatus)){
                printf("background pid %d is done: exit value %d\n", bg_pid, WEXITSTATUS(childExitStatus));
                fflush(stdout);
                bg_processes[i] = 0;

            } else if (WIFSIGNALED(childExitStatus)){
                printf("background pid %d is done: terminated by signal %d\n", bg_pid, WTERMSIG(childExitStatus));
                fflush(stdout);
                bg_processes[i] = 0;

            } else {
                printf("background pid %d is done: stopped\n", bg_pid); // Shouldn't happen in this program
                fflush(stdout);
                bg_processes[i] = 0;
            }
        }
    }

}

// Command line function which inserts a pid into an array of pids at the first open spot
// Arguments: A pointer to an array of integers corresponding to pids
// Returns: void
void insert_into_bg_process_array(int bg_processes[], int pid){

    int bg_pid;

    for(int i = 0; i < MAX_BG_COMMANDS; i++){
        bg_pid = bg_processes[i];
        if (bg_pid == 0){
            bg_processes[i] = pid;
            break;
        }
    }

}

// Command line function which redirects standard in to the source argument
// Arguments: A string which contains a path to repoint stdin to
// Returns: 0 on success, -1 on error
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

// Command line function which redirects standard out to the destination argument
// Arguments: A string which contains a path to repoint stdout to
// Returns: 0 on success, -1 on error
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

// Signal handler function for SIGTSTP which changes the state of foreground-only mode
// In foreground only mode, background processes are run as if they were foreground processes
// Arguments: A signal number integer
// Returns: void
void handle_SIGTSTP(int signo){ //Ctrl-Z
    
    // Change background ignore to opposite state
    if (!ignore_bg){
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
        ignore_bg = true;

    } else {
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        ignore_bg = false;
    }

}

// Signal handler function for SIGINT which sends a SIGKILL signal to the current process
// Arguments: A signal number integer
// Returns: void
void handle_SIGINT(int signo){ //Ctrl-C

    raise(SIGKILL);

}