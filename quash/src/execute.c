/**
updated
 * @file execute.c
 *
 * @brief Implements interface functions between Quash and the environment and
 * functions that interpret and execute commands.
 */

 #include "execute.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <fcntl.h>
 #include <sys/wait.h>
 #include <string.h>
 #include "quash.h"
 
 /***************************************************************************
  * Interface Functions
  ***************************************************************************/
 
 // Return a string containing the current working directory.
 char* get_current_directory(bool* should_free) {
     char* cwd = getcwd(NULL, 0);
     if (!cwd) {
         perror("ERROR: Failed to get current directory");
         return NULL;
     }
     *should_free = true;
     return cwd;
 }
 
 // Returns the value of an environment variable env_var
 const char* lookup_env(const char* env_var) {
     const char* value = getenv(env_var);
     return value ? value : "";
 }
 
 // Check the status of background jobs
 void check_jobs_bg_status() {
     int job_id, pid;
     char* cmd;
 
     #ifdef get_finished_job  // Ensure function exists
         while (get_finished_job(&job_id, &pid, &cmd)) {
             print_job_bg_complete(job_id, pid, cmd);
         }
     #endif
 }
 
 // Prints job details
 void print_job(int job_id, pid_t pid, const char* cmd) {
     printf("[%d]\t%8d\t%s\n", job_id, pid, cmd);
     fflush(stdout);
 }
 
 // Prints a start-up message for background processes
 void print_job_bg_start(int job_id, pid_t pid, const char* cmd) {
     printf("Background job started: ");
     print_job(job_id, pid, cmd);
 }
 
 // Prints a completion message followed by the print job
 void print_job_bg_complete(int job_id, pid_t pid, const char* cmd) {
     printf("Completed: \t");
     print_job(job_id, pid, cmd);
 }
 
 /***************************************************************************
  * Functions to process commands
  ***************************************************************************/
 
 // Runs a program with the given arguments
 void run_generic(GenericCommand cmd) {
     if (cmd.args == NULL || cmd.args[0] == NULL) {
         fprintf(stderr, "ERROR: No command provided.\n");
         return;
     }
 
     pid_t pid = fork();
 
     if (pid < 0) {
         perror("ERROR: Fork failed");
         return;
     }
 
     if (pid == 0) { // Child process
         execvp(cmd.args[0], cmd.args);
         perror("ERROR: Failed to execute command");
         _exit(EXIT_FAILURE);
     } else { // Parent process
         if (!(cmd.flags & BACKGROUND)) {
             waitpid(pid, NULL, 0); // Foreground process
         } else {
             int job_id = add_background_job(pid, cmd.args[0]);
             print_job_bg_start(job_id, pid, cmd.args[0]);
         }
     }
 }
 
 // Prints strings
 void run_echo(EchoCommand cmd) {
     for (char** arg = cmd.args; *arg != NULL; ++arg) {
         printf("%s ", *arg);
     }
     printf("\n");
     fflush(stdout);
 }
 
 // Sets an environment variable
 void run_export(ExportCommand cmd) {
     if (setenv(cmd.env_var, cmd.val, 1) != 0) {
         perror("ERROR: Failed to set environment variable");
     }
 }
 
 // Changes the current working directory
 void run_cd(CDCommand cmd) {
     if (cmd.dir == NULL) {
         perror("ERROR: Failed to resolve path");
         return;
     }
 
     // Expand to absolute path
     char* real_path = realpath(cmd.dir, NULL);
     if (real_path == NULL) {
         perror("ERROR: Failed to resolve absolute path");
         return;
     }
 
     if (chdir(real_path) != 0) {
         perror("ERROR: Failed to change directory");
     } else {
         char cwd[1024];
         if (getcwd(cwd, sizeof(cwd)) != NULL) {
             setenv("PWD", cwd, 1);
         }
     }
     free(real_path);
 }
 
 // Sends a signal to all processes in a job
 void run_kill(KillCommand cmd) {
     if (kill(cmd.job, cmd.sig) != 0) {
         perror("ERROR: Failed to send signal");
     }
 }
 
 // Prints the current working directory
 void run_pwd() {
     char cwd[1024];
     if (getcwd(cwd, sizeof(cwd)) != NULL) {
         printf("%s\n", cwd);
     } else {
         perror("ERROR: Failed to get current directory");
     }
     fflush(stdout);
 }
 
 // Prints all background jobs currently in the job list
 void run_jobs() {
     list_background_jobs();
     fflush(stdout);
 }
 
 /***************************************************************************
  * Functions for command resolution and process setup
  ***************************************************************************/
 
 // Resolves and runs the appropriate command in the child process
 void child_run_command(Command cmd) {
     switch (get_command_type(cmd)) {
     case GENERIC:
         run_generic(cmd.generic);
         break;
     case ECHO:
         run_echo(cmd.echo);
         break;
     case PWD:
         run_pwd();
         break;
     case JOBS:
         run_jobs();
         break;
     default:
         fprintf(stderr, "Unknown command type.\n");
     }
 }
 
 // Resolves and runs commands that should execute in the parent process
 void parent_run_command(Command cmd) {
     switch (get_command_type(cmd)) {
     case EXPORT:
         run_export(cmd.export);
         break;
     case CD:
         run_cd(cmd.cd);
         break;
     case KILL:
         run_kill(cmd.kill);
         break;
     default:
         break;
     }
 }
 
 // Creates one new process centered around a command
 void create_process(CommandHolder holder) {
     int fds[2];
     if (holder.flags & PIPE_OUT) pipe(fds);
 
     pid_t pid = fork();
 
     if (pid == 0) { // Child process
         if (holder.flags & REDIRECT_IN) {
             int fd = open(holder.redirect_in, O_RDONLY);
             dup2(fd, STDIN_FILENO);
             close(fd);
         }
         if (holder.flags & REDIRECT_OUT) {
             int fd = open(holder.redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
             dup2(fd, STDOUT_FILENO);
             close(fd);
         }
         if (holder.flags & PIPE_OUT) {
             dup2(fds[1], STDOUT_FILENO);
             close(fds[0]);
             close(fds[1]);
         }
         child_run_command(holder.cmd);
         _exit(EXIT_FAILURE);
     } else {
         if (holder.flags & PIPE_OUT) {
             close(fds[1]); // Only close write end in parent
         }
         parent_run_command(holder.cmd);
     }
 }
 
 // Runs a list of commands
 void run_script(CommandHolder* holders) {
     if (!holders || get_command_holder_type(holders[0]) == EOC) return;
 
     check_jobs_bg_status();
 
     for (int i = 0; get_command_holder_type(holders[i]) != EOC; ++i)
         create_process(holders[i]);
 
     if (holders && !(holders[0].flags & BACKGROUND)) {
         for (int i = 0; get_command_holder_type(holders[i]) != EOC; ++i) {
             wait(NULL);
         }
     }
 }
 