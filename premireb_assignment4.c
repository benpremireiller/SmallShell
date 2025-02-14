#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h> // for waitpid

#define INPUT_LENGTH 2048
#define MAX_ARGS 512


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

void cleanup_exit(){

    // TODO terminate running processes
    return;

}

void print_status(int status){

    printf("%d", status);
}

int main(){

    int status = 0;
    int spawnpid;
    int childExitStatus;
    struct command_line *curr_command;
    int destinationFD;
    int sourceFD;
    int background_processes[];

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
            cleanup_exit();
            return EXIT_SUCCESS;
            
        } else {

            spawnpid = fork();
            switch (spawnpid){
                case -1:
                    perror("fork() failed!");
                    exit(1);
                    break;

                case 0: // Child
                    
                    // Redirect streams if necessary
                    if(curr_command->input_file){
                        sourceFD = open(curr_command->input_file, O_RDONLY);
                        if (sourceFD == -1){
                            printf("cannot open %s for input", curr_command->input_file);
                            fflush(stdout);
                            status = 1;
                            break;
                        }
                        dup2(sourceFD, stdin);
                    }

                    if(curr_command->output_file){
                        destinationFD = open(curr_command->output_file, O_WRONLY);
                        if (destinationFD == -1){
                            printf("cannot open %s for output", curr_command->destination_file);
                            fflush(stdout);
                            status = 1; // Question for myself, what does the child return to if the whole program gets overwritten with exec?
                            break;
                        }
                        dup2(destinationFD, stdout);
                    }
                    
                    // Run command
                    (curr_command->argv)[curr_command->argc+1] = NULL; // Add null terminator
                    execvp(curr_command->command, curr_command->argv);
                    perror("execvp"); // Only get here if exec fails
                    exit(2);
                    break;
        
                default: // Parent
                    
                    spawnpid = waitpid(spawnpid, &childExitStatus, 0);
                    if (WIFEXITED(childExitStatus)){
                        status = 1; 
                    }
                    break;
            }
        }

    }
    return EXIT_SUCCESS;
}
