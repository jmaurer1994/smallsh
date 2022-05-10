/***
 *      .-')   _   .-')      ('-.                          .-')    ('-. .-.
 *     ( OO ).( '.( OO )_   ( OO ).-.                     ( OO ). ( OO )  /
 *    (_)---\_),--.   ,--.) / . --. / ,--.      ,--.     (_)---\_),--. ,--.
 *    /    _ | |   `.'   |  | \-.  \  |  |.-')  |  |.-') /    _ | |  | |  |
 *    \  :` `. |         |.-'-'  |  | |  | OO ) |  | OO )\  :` `. |   .|  |
 *     '..`''.)|  |'.'|  | \| |_.'  | |  |`-' | |  |`-' | '..`''.)|       |
 *    .-._)   \|  |   |  |  |  .-.  |(|  '---.'(|  '---.'.-._)   \|  .-.  |
 *    \       /|  |   |  |  |  | |  | |      |  |      | \       /|  | |  |
 *     `-----' `--'   `--'  `--' `--' `------'  `------'  `-----' `--' `--'
 * Author: Joe Maurer
 *
 * Description: This is a shell program, written in C. Contains built in
 * support for 3 commands:
 *
 *  cd          - change directory
 *  status      - provides the exit status of the program, or last child if any
 *                have been terminated.
 *  exit/quit   - terminates the shell program and any child processes (hotkey:
 *                ctrl^\) foreground and background.
 *
 * Any other commands are handled by a call to an exec() function with provided
 * arguments. Shell supports input/output redirection as well as optional
 * background execution (&).
 *
 * Command syntax:
 *
 *  command [arg1 arg2 ...] [< input_file] [> output_file] [&]
 *
 * Instructions:
 *
 * Compile with
 *  gcc -std=c99 -Wall -g -o smallsh smallsh.c -lm
 *
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
/*******************************************************************************
 * Structures
 *
 ******************************************************************************/
struct UserInputStruct // struct to hold payload for command
{
    char **argv; // must terminated with a NULL pointer for exec
    char *inputDestination_ptr;
    char *outputDestination_ptr;
    int *runInBackground;
    int *checkSum;
};
typedef struct UserInputStruct UserInputStruct;

// do not look at these
int currentStatus = 0;
int fgOnly = 0;
int control_var = 1;
sig_atomic_t quit = 0;

/******************************************************************************
 * Signal handlers
 *
 ******************************************************************************/
void handle_SIGCHLD(int signo, siginfo_t *siginfo, void *ucontext) {
    pid_t spawnpid;
    int status;
    while ((spawnpid = waitpid(-1, &status, WNOHANG)) > 0) {
        currentStatus = status;
        char *message = "\nBackground process (";
        write(STDOUT_FILENO, message, 21);

        int digit = 0;

        int tempPid = 0;
        tempPid = (int)spawnpid;

        size_t nDigits = floor(log10(abs(tempPid))) + 1;
        char pidChar[nDigits];

        size_t count = 1;
        while (tempPid > 0) {
            digit = tempPid % 10;
            tempPid = tempPid / 10;
            pidChar[nDigits - count] = (char)(digit + 48);
            count++;
        }

        write(STDOUT_FILENO, pidChar, nDigits);

        char *message2 = ") is done: ";
        write(STDOUT_FILENO, message2, 11);

        int tempStatus;

        if (WIFEXITED(status)) {
            char *message3 = "exit value ";
            write(STDOUT_FILENO, message3, 11);

            tempStatus = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            char *message3 = "terminated by signal ";
            write(STDOUT_FILENO, message3, 21);
            tempStatus = WTERMSIG(status);
        }

        count = 1;
        digit = 0;
        if (tempStatus < 10) {
            char statusChar[] = {(char)(status + 48)};
            write(STDOUT_FILENO, statusChar, (size_t)1);
        } else {
            nDigits = floor(log10(abs(tempStatus))) + 1;

            char statusChar[nDigits];

            while (tempStatus > 0) {
                digit = tempStatus % 10;
                tempStatus = tempStatus / 10;
                statusChar[nDigits - count] = (char)(digit + 48);
                count++;
            }
            write(STDOUT_FILENO, statusChar, nDigits);
        }

        if (WIFEXITED(status)) {
            char *message4 = "\n";
            write(STDOUT_FILENO, message4, 2);
        } else {
            char *message4 = "\n";
            write(STDOUT_FILENO, message4, 1);
        }
    }
    fflush(stdout);
}

void handle_SIGTSTP(int signo, siginfo_t *siginfo, void *ucontext) {
    fgOnly = !fgOnly;

    if (fgOnly) {
        char *message = "\nNow entering foreground only mode\n";
        write(STDOUT_FILENO, message, 36);
        fflush(stdout);
    } else {
        char *message = "\nNow leaving foreground only mode\n";
        write(STDOUT_FILENO, message, 35);
        fflush(stdout);
    }
}

void handle_SIGUSR1(int signo, siginfo_t *siginfo, void *ucontext) {

    char *message =
        "\nSmallsh encountered an error\nAttemping to recover...\n: ";
    write(STDOUT_FILENO, message, 56);
    fflush(stdout);
    return;
}

/*****************************************************************
 * TODO
 * 
 * This doesn't work quite how I want it to, re: ctrl+\
 *
 * **************************************************************/
void handle_SIGQUIT(int signo, siginfo_t *siginfo, void *ucontext) {
    quit = 1;
}

/*******************************************************************************
 * Functions
 *
 ******************************************************************************/

/*******************************************************************************
 * getInputString
 *
 *  Description
 *      Returns pointer to a dynamically allocated string retrieved by getline
 *      from stdin. Removes trailing \n character. Performs expansion for $$
 *
 *  Inputs:
 *      Retrieves user input from stdin
 *
 *  Outputs:
 *      Returns a char* on success. Returns null on failure.
 ******************************************************************************/
char *getInputString() {
    while (1) {
        char *userInputString = NULL;
        char *expansion_str = NULL;
        size_t userInputStringLength = 0;
        char *temp_str = NULL;

        fprintf(stdout, ": ");
        fflush(stdout);

        fflush(stdin);
        /*****************************************************************
         * TODO
         * This allocation is causing a memory leak when execvp fails to
         * execute I can't quit fix yet.
         *
         * **************************************************************/
        ssize_t nRead = getline(&temp_str, &userInputStringLength, stdin);
        fflush(stdin);
        if (temp_str == NULL) {
            raise(SIGUSR1);
            return NULL;
        }

        // await user input
        if (nRead > 1) {
            // successfully read
            if (temp_str[0] == '#') {
                // ignore comment
                continue;
            }

            // begone \n
            temp_str[nRead - (size_t)(1)] = '\0';

            // handle $$ expansion
            char *moneyPtr = strstr(temp_str, "$$");
            if (moneyPtr != NULL) {
                // find how many times we're expanding $$
                int numOccurences = 0;
                for (size_t i = 0; i < strlen(temp_str); i++) {
                    if (temp_str[i] == '$') {
                        if (temp_str[i + (size_t)1] == '$') {
                            numOccurences++;

                            i = i + 1; // consume the
                                       // second $
                        }
                    }
                }

                // do some math to calc new token size
                size_t byteCount = 0;
                pid_t pid = getpid();
                size_t nDigits = floor(log10(abs(pid))) + 1;

                byteCount = (strlen(temp_str) - (2 * numOccurences)) +
                            (nDigits * numOccurences) + 1;
                //(strlen - 2*numoccurences) - string length of
                // original tokens minus however many $ we're
                // removing (nDigits * numOccurences) - digits of
                // the pid * how many times we're inserting it +1
                // for null terminator

                // convert the pid to a char[]
                char pidChar[nDigits];
                int digit = 0;
                size_t count = 1;
                while (pid > 0) {
                    digit = pid % 10;
                    pid = pid / 10;
                    pidChar[nDigits - count] = (char)(digit + 48);
                    count++;
                }

                // allocate some space
                expansion_str = calloc(byteCount, sizeof(char));
                if (expansion_str == NULL) {
                    free(temp_str);
                    raise(SIGUSR1);
                    return userInputString;
                }

                size_t offset = 0;
                // build the new token
                for (size_t h = 0; h < strlen(temp_str); h++) {
                    if (temp_str[h] == '$') {
                        if (temp_str[h + (size_t)1] == '$') {
                            // add the pid to the arr
                            for (size_t j = 0; j < nDigits; j++) {
                                expansion_str[h + j + offset] = pidChar[j];
                            }

                            offset = offset + nDigits - 2;
                            h = h + 1;
                        } else {
                            expansion_str[h + offset] = '$';
                        }
                    } else {
                        expansion_str[h + offset] = temp_str[h];
                    }
                }
            }

            if (expansion_str != NULL) {
                // expansion perfomed
                free(temp_str);
                temp_str = expansion_str;
            }

            // attempt to allocate space for input string
            userInputString = calloc(strlen(temp_str) + 1, sizeof(char));
            if (userInputString == NULL) {
                free(userInputString);
                raise(SIGUSR1);
                return NULL;
            }

            strcpy(userInputString, temp_str);
            free(temp_str);

            return userInputString;
        } else if (nRead < 1) {
            free(userInputString);
            clearerr(stdin);
            return NULL;
        } else {

            free(temp_str);
            // user entered nothing, reprompt
        }
    }
}

/*******************************************************************************
 * parseToken()
 *  Description:
 *      Recursively parses tokens out of the original input string and populates
 *      the non-argument attributes of the UserInputStruct. 
 * 
 *      Counts and sets argc so that the calling function can create argv[];
 *  
 *  Inputs:
 *      UserInputStruct userInput
 *      char* token
 *      size_t* argc
 *
 *  Outputs:
 *      Allocates a pointer for, and sets the value of argc
 *      parses < , > , & out of the token array and builds the userInput struct
 *
 ******************************************************************************/
void parseToken(UserInputStruct userInput, char *token, size_t *argc) {
    // lets take a look at the token
    if (token == NULL) {
        return;
    }

    if (strcmp(token, "<") == 0) {
        // get next token and set input destination
        token = strtok(NULL, " ");

        strcpy(userInput.inputDestination_ptr, token);

        // get next token and recurse
        token = strtok(NULL, " ");
        parseToken(userInput, token, argc);
    } else if (strcmp(token, ">") == 0) {
        // get next token and set output destination
        token = strtok(NULL, " ");

        strcpy(userInput.outputDestination_ptr, token);

        // get next token and recurse
        token = strtok(NULL, " ");
        parseToken(userInput, token, argc);
    } else if (strcmp(token, "&") == 0) {
        // get next token and see if it's NULL, otherwise ignore
        token = strtok(NULL, " ");
        if (token == NULL) {
            // User would like to run in background
            if (!fgOnly) {
                *userInput.runInBackground = 1;
            }
        } else {
            // if the next token still contains text, ignore it and
            // continue parsing the line

            parseToken(userInput, token, argc);
        }
    } else {
        //  item is command or arg, count it
        *argc = *argc + (size_t)1;

        // get next token and recurse
        token = strtok(NULL, " ");
        parseToken(userInput, token, argc);
    }
}

/*******************************************************************************
 * getuserInputFromString()
 *
 * Inputs:
 *  char* userInputString
 *
 * Outputs:
 *  Returns a UserInputStruct containing the necessary info to execute a
 *  command sent by the user.
 *
 ******************************************************************************/
UserInputStruct getuserInputFromString(char *userInputString) {

    char *defaultDestination = "/dev/null";

    // initialize the struct
    UserInputStruct userInput;
    userInput.argv = NULL;
    userInput.inputDestination_ptr = NULL;
    userInput.outputDestination_ptr = NULL;
    userInput.runInBackground = NULL;

    userInput.checkSum = malloc(sizeof(int));
    // check to see if checksum allocated
    if (userInput.checkSum == NULL) {
        raise(SIGUSR1);
        return userInput;
    } else {
        *userInput.checkSum = 0;
    }

    // attempt our first allocations
    userInput.inputDestination_ptr =
        calloc(strlen(defaultDestination) + (size_t)1, (size_t)1);
    if (userInput.inputDestination_ptr == NULL) {
        raise(SIGUSR1);
        return userInput;
    }

    userInput.outputDestination_ptr =
        calloc(strlen(defaultDestination) + (size_t)1, (size_t)1);
    if (userInput.outputDestination_ptr == NULL) {
        raise(SIGUSR1);
        return userInput;
    }

    userInput.runInBackground = malloc(sizeof(int));
    if (userInput.runInBackground == NULL) {
        raise(SIGUSR1);
        return userInput;
    }

    strcpy(userInput.inputDestination_ptr, defaultDestination);
    strcpy(userInput.outputDestination_ptr, defaultDestination);

    *userInput.runInBackground = 0;

    // let's build the arg array
    userInput.argv = NULL;

    // allocate a second string to hold a copy of the input... for reasons...
    char *inputString = calloc(strlen(userInputString) + 1, sizeof(char));
    if (inputString == NULL) {
        raise(SIGUSR1);
        return userInput;
    }

    strcpy(inputString, userInputString);

    char *token;
    size_t argc = 0;
    token = strtok(inputString, " ");

    // check to see that we got a non-empty token
    if (token != NULL) {
        parseToken(userInput, token, &argc);
    }

    free(inputString);

    userInput.argv =
        malloc((argc + (size_t)1) *
               (size_t)sizeof(char *)); // extra item for NULL pointer so we
                                        // know we've hit the end of the array
    if (userInput.argv == NULL) {
        raise(SIGUSR1);
        return userInput;
    }

    // loop through the string again to get the args:
    for (size_t i = 0; i < argc; i++) {
        token = NULL;
        if (i == 0) {
            // first time through the loop
            token = strtok(userInputString, " ");
        } else {
            token = strtok(NULL, " ");
        }

        userInput.argv[i] =
            calloc(strlen(token) + 1,
                   sizeof(char)); // extra space for null terminator, use calloc
                                  // so that memory space is initialized to \0
        if (userInput.argv[i] == NULL) {
            raise(SIGUSR1);
            return userInput;
        }

        // add the token to the array
        strcpy(userInput.argv[i], token);
    }

    // done processing args, append null pointer
    userInput.argv[argc] = (void *)NULL;

    // we made it through without returning early.
    *userInput.checkSum = 1;

    return userInput;
}

/*******************************************************************************
 * freeUserInput()
 *
 * Purpose: attempts to free up allocated memory within the UserInputStruct.
 *
 * Inputs:
 *  UserInputStruct userInput
 *
 ******************************************************************************/
void freeUserInput(UserInputStruct userInput) {
    size_t i = 0;

    // free the argv contents
    while (1) {
        if (userInput.argv[i] != NULL) {
            free(userInput.argv[i]);
            i++;
        } else {
            break;
        }
    }

    // free argv itself
    free(userInput.argv);

    // free the others
    free(userInput.inputDestination_ptr);
    free(userInput.outputDestination_ptr);
    free(userInput.runInBackground);
    free(userInput.checkSum);
    return;
}

/*******************************************************************************
 * main()
 *
 ******************************************************************************/
int main() {
    if (control_var) {
        fprintf(stdout,
                "\nWelcome to smallsh\nPress ctrl^c to interrupt a process, "
                "ctrl^z to toggle foreground-only mode, or ctrl^\\ to "
                "quit.\n");
        fflush(stdout);
        control_var = 0;
    }

    // main execution loop
    // register event handlers
    struct sigaction SIGINT_action = {{0}};
    sigemptyset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_handler = SIG_IGN;

    struct sigaction SIGTSTP_action = {{0}};
    sigemptyset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_sigaction = handle_SIGTSTP;
    SIGTSTP_action.sa_flags = SA_SIGINFO;

    struct sigaction SIGUSR1_action = {{0}};
    sigemptyset(&SIGUSR1_action.sa_mask);
    SIGUSR1_action.sa_sigaction = handle_SIGUSR1;
    SIGUSR1_action.sa_flags = SA_SIGINFO;

    struct sigaction SIGQUIT_action = {{0}};
    sigemptyset(&SIGQUIT_action.sa_mask);
    SIGQUIT_action.sa_sigaction = handle_SIGQUIT;
    SIGQUIT_action.sa_flags = SA_RESTART | SA_SIGINFO;

    sigaction(SIGINT, &SIGINT_action, NULL);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    sigaction(SIGUSR1, &SIGUSR1_action, NULL);
    sigaction(SIGQUIT, &SIGQUIT_action, NULL);
    
    while (!quit) {

        // Get input from the user
        char *inputString = NULL;
        while (1) {
            inputString = getInputString();

            if (inputString == NULL) {
                continue;
            } else {
                break;
            }
        }

        // parse the input
        UserInputStruct userInput;
        while (1) {
            userInput = getuserInputFromString(inputString);

            /*****************************************************************
             * TODO
             * This validation logic should be looked at again to make sure
             * it's clearing everything properly (i dont think it is from a
             * quick sleep deprived glance)
             *
             * **************************************************************/
            // validate the input in the order it was created
            if (userInput.checkSum == NULL) {
                fprintf(stderr, "Checksum allocation failed\n");
                fflush(stderr);
            } else if (userInput.checkSum == 0) {
                fprintf(stderr, "Input allocation failed ");
                if (userInput.inputDestination_ptr == NULL) {
                    fprintf(stderr, "...during input destination "
                                    "allocation\n");
                    fflush(stderr);
                } else if (userInput.outputDestination_ptr == NULL) {
                    fprintf(stderr, "...during output destination "
                                    "allocation\n");
                    fflush(stderr);
                } else if (userInput.runInBackground == NULL) {
                    fprintf(stderr, "...background boolean allocation\n");
                    fflush(stderr);
                } else if (userInput.argv == NULL) {
                    fprintf(stderr, "...during pointer array "
                                    "allocation\n");
                    fflush(stderr);
                } else {

                    // check arguements.
                    size_t i = 0;
                    while (1) {
                        if (userInput.argv[i] != NULL) {
                            free(userInput.argv[i]);
                            i++;
                        } else {
                            break;
                        }
                    }

                    fprintf(stderr,
                            "...during an argument "
                            "allocation\n Read %zu args "
                            "successfully before error.\n",
                            i);
                    fflush(stderr);
                }
            } else {
                break;
            }
        }

        free(inputString);

        // execute the input

        /*****************************************************************
         * TODO 
         * This should probably be handled better in its own function
         * and not in main
         *
         * **************************************************************/
        if (strcmp(userInput.argv[0], "cd") == 0) {
            if (userInput.argv[1] == NULL) {
                if (chdir(getenv("HOME")) != 0) {
                    fprintf(stderr, "Encountered an error "
                                    "while attempting to "
                                    "open home directory.\n");
                    fflush(stderr);
                }
            } else {
                if (chdir(userInput.argv[1]) != 0) {
                    fprintf(stderr, "Directory not found, "
                                    "please try again.\n");
                    fflush(stderr);
                }
            }
            // command executed ok
        } else if (strcmp(userInput.argv[0], "status") == 0) {
            if (WIFEXITED(currentStatus)) {
                fprintf(stdout, "exit value %d\n", WEXITSTATUS(currentStatus));
                fflush(stdout);
            } else if (WIFSIGNALED(currentStatus)) {
                fprintf(stdout, "terminated by signal %d\n",
                        WTERMSIG(currentStatus));
                fflush(stdout);
            }

            fflush(stdout);
        } else if (strcmp(userInput.argv[0], "exit") == 0 ||
                   strcmp(userInput.argv[0], "quit") == 0) {
            raise(SIGQUIT);
        } else {
            // else process command for exec
            int childStatus;
            int inputDestination;
            int outputDestination;

            // open redirection destinations
            inputDestination =
                open(userInput.inputDestination_ptr, O_RDONLY, 0444);
            outputDestination = open(userInput.outputDestination_ptr,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0666);
            // fork a new process
            pid_t spawnPid = fork();

            if (spawnPid < 0) {
                fprintf(stderr, "fork(): ");
                fflush(stderr);
                freeUserInput(userInput);
                close(inputDestination);
                close(outputDestination);
                exit(2);
            } else if (spawnPid == 0) {
                /**********************************
                 * CHILD PROCESS
                 *********************************/

                // reregister signal handling
                // fg child should respond to sigint
                struct sigaction SIGINT_action_child_fg = {{0}};
                sigemptyset(&SIGINT_action_child_fg.sa_mask);
                SIGINT_action_child_fg.sa_handler = SIG_DFL;

                // bg child should ignore
                struct sigaction SIGINT_action_child_bg = {{0}};
                sigemptyset(&SIGINT_action_child_bg.sa_mask);
                SIGINT_action_child_bg.sa_handler = SIG_IGN;

                // ignore everything else too
                struct sigaction SIGCHLD_action_child = {{0}};
                sigemptyset(&SIGCHLD_action_child.sa_mask);
                SIGCHLD_action_child.sa_handler = SIG_IGN;

                struct sigaction SIGTSTP_action_child = {{0}};
                sigemptyset(&SIGTSTP_action_child.sa_mask);
                SIGTSTP_action_child.sa_handler = SIG_IGN;

                struct sigaction SIGUSR1_action_child = {{0}};
                sigemptyset(&SIGUSR1_action_child.sa_mask);
                SIGUSR1_action.sa_handler = SIG_IGN;

                struct sigaction SIGQUIT_action_child = {{0}};
                sigemptyset(&SIGQUIT_action_child.sa_mask);
                SIGQUIT_action.sa_handler = SIG_IGN;

                // check to see if files opened, kill the child if
                // it failed
                if (inputDestination < 0) {
                    fprintf(stderr,
                            "Can not open file for input redirection\n");
                    fflush(stderr);
                    freeUserInput(userInput);
                    exit(2);
                }
                if (outputDestination < 0) {
                    fprintf(stderr,
                            "Can not open file for output redirection\n");
                    fflush(stderr);
                    close(inputDestination);
                    freeUserInput(userInput);
                    exit(2);
                }

                if (*userInput.runInBackground) {
                    /**********************************
                     * BACKGROUND CHILD
                     *********************************/
                    // stdin/out should be redirected
                    // regardless
                    dup2(inputDestination, 0);
                    dup2(outputDestination, 1);

                    // set bg process to ignore SIGINT
                    sigaction(SIGINT, &SIGINT_action_child_bg, NULL);
                } else {
                    /**********************************
                     * FOREGROUND CHILD
                     *********************************/
                    // only redirect if the user chooses to
                    if (strcmp(userInput.inputDestination_ptr, "/dev/null") !=
                        0) {
                        dup2(inputDestination, 0);
                    }
                    if (strcmp(userInput.outputDestination_ptr, "/dev/null") !=
                        0) {
                        dup2(outputDestination, 1);
                    }

                    sigaction(SIGINT, &SIGINT_action_child_fg, NULL);
                }

                sigaction(SIGTSTP, &SIGTSTP_action_child, NULL);
                sigaction(SIGCHLD, &SIGCHLD_action_child, NULL);
                sigaction(SIGUSR1, &SIGUSR1_action_child, NULL);
                sigaction(SIGQUIT, &SIGQUIT_action_child, NULL);

                execvp(userInput.argv[0], userInput.argv);
                // exec only returns here if there is an error
                fprintf(stdout, "Command not found or failed to execute\n");
                fflush(stdout);

            } else {
                /**********************************
                 * PARENT PROCESS
                 *********************************/
                struct sigaction SIGCHLD_action = {{0}};
                sigemptyset(&SIGCHLD_action.sa_mask);
                SIGCHLD_action.sa_sigaction = handle_SIGCHLD;
                SIGCHLD_action.sa_flags = SA_SIGINFO;
                sigaction(SIGCHLD, &SIGCHLD_action, NULL);

                if (*userInput.runInBackground) {

                    fprintf(stdout, "Background process PID:(%d)\n", spawnPid);
                    fflush(stdout);
                } else {
                    struct sigaction temp_act = {{0}};
                    sigemptyset(&temp_act.sa_mask);

                    // temporarily block sigtstp sigchld
                    // sigusr1 sigquit
                    sigaddset(&temp_act.sa_mask, SIGTSTP);
                    sigaddset(&temp_act.sa_mask, SIGCHLD);
                    sigaddset(&temp_act.sa_mask, SIGUSR1);
                    sigaddset(&temp_act.sa_mask, SIGQUIT);

                    sigprocmask(SIG_BLOCK, &temp_act.sa_mask, NULL);

                    spawnPid = waitpid(spawnPid, &childStatus, 0);
                    currentStatus = childStatus;
                    if (WIFSIGNALED(currentStatus)) {
                        fprintf(stdout, "terminated by signal %d\n",
                                WTERMSIG(currentStatus));
                    }

                    sigprocmask(SIG_UNBLOCK, &temp_act.sa_mask, NULL);
                }
            }

            close(inputDestination);
            close(outputDestination);
        }

        freeUserInput(userInput);
    }

    struct sigaction temp_action = {{0}};
    sigemptyset(&temp_action.sa_mask);
    sigaddset(&temp_action.sa_mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &temp_action.sa_mask, NULL);
    // kill children
    kill(0, SIGTERM);

    int childStatus = NULL;
    pid_t pid = wait(&childStatus);
    while (pid > 0) {
        // wait for child processes to finish
        if (WIFSIGNALED(childStatus)) {
            fprintf(stdout,
                    "\nBackground process (%d) is done: terminated "
                    "by signal %d",
                    pid, WTERMSIG(childStatus));

            fflush(stdout);
        }
        pid = wait(&childStatus);
    }

    fprintf(stdout, "\n\nThank you for using smallsh\n");
    fflush(stdout);
    sigprocmask(SIG_UNBLOCK, &temp_action.sa_mask, NULL);
}
