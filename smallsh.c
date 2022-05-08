#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h> // for waitpid

#ifndef UINT8_MAX
#error "No support for uint8_t"
#endif

// do not look at these
int currentStatus = 0;
int fgOnly = 0;

/*************************************************************************************************************************************
 * Structures
 *
 *
 *
 *
 ************************************************************************************************************************************/
struct UserInputStruct // struct to hold payload for command
{
    char **argv; // must terminated with a NULL pointer for exec
    char *inputDestination_ptr;
    char *outputDestination_ptr;
    int *runInBackground;
    int *checkSum;
};
typedef struct UserInputStruct UserInputStruct;

/*************************************************************************************************************************************
 * Signal handlers
 *
 *
 *
 *
 ************************************************************************************************************************************/
void handle_SIGCHLD(int signo, siginfo_t *siginfo, void *ucontext)
{
    pid_t spawnpid;
    int status;
    while ((spawnpid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        currentStatus = status;
        char *message = "\nBackground process (";
        write(STDOUT_FILENO, message, 21);

        int digit = 0;

        int tempPid = 0;
        tempPid = (int)spawnpid;

        size_t nDigits = floor(log10(abs(tempPid))) + 1;
        char pidChar[nDigits];

        size_t count = 1;
        while (tempPid > 0)
        {
            digit = tempPid % 10;
            tempPid = tempPid / 10;
            pidChar[nDigits - count] = (char)(digit + 48);
            count++;
        }

        write(STDOUT_FILENO, pidChar, nDigits);

        char *message2 = ") is done: ";
        write(STDOUT_FILENO, message2, 11);

        int tempStatus;

        if (WIFEXITED(status))
        {
            char *message3 = "exit value ";
            write(STDOUT_FILENO, message3, 11);

            tempStatus = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            char *message3 = "terminated by signal ";
            write(STDOUT_FILENO, message3, 21);
            tempStatus = WTERMSIG(status);
        }

        count = 1;
        digit = 0;
        if (tempStatus < 10)
        {
            char statusChar[] = {(char)(status + 48)};
            write(STDOUT_FILENO, statusChar, (size_t)1);
        }
        else
        {
            nDigits = floor(log10(abs(tempStatus))) + 1;

            char statusChar[nDigits];

            while (tempStatus > 0)
            {
                digit = tempStatus % 10;
                tempStatus = tempStatus / 10;
                statusChar[nDigits - count] = (char)(digit + 48);
                count++;
            }
            write(STDOUT_FILENO, statusChar, nDigits);
        }

        if (WIFEXITED(status))
        {
            char *message4 = "\n: ";
            write(STDOUT_FILENO, message4, 3);
        }
        else
        {
            char *message4 = "\n";
            write(STDOUT_FILENO, message4, 1);
        }
    }
    fflush(stdout);
}

void handle_SIGTSTP(int signo, siginfo_t *siginfo, void *ucontext)
{
    fgOnly = !fgOnly;

    if (fgOnly)
    {
        char *message = "\nNow entering foreground only mode\n: ";
        write(STDOUT_FILENO, message, 37);
        fflush(NULL);
    }
    else
    {
        char *message = "\nNow Leaving foreground only mode\n: ";
        write(STDOUT_FILENO, message, 36);
        fflush(NULL);
    }
}

void handle_SIGUSR1(int signo, siginfo_t *siginfo, void *ucontext)
{

    char *message = "\nSmallsh encountered an error during memory allocation\nAttemping to recover...\n: ";
    write(STDOUT_FILENO, message, 81);
    fflush(NULL);
    return;
}

/*************************************************************************************************************************************
 * Functions
 *
 *
 *
 *
 *
 *
 ************************************************************************************************************************************/

/**********************************************************************************************
 * getInputString
 *
 * Returns pointer to a string retrieved by getline from stdin. Removes trailing \n character
 *
 *********************************************************************************************/
char *getInputString()
{
    while (1)
    {
        char *userInput = NULL;
        char *tempStr = NULL;
        size_t userInputLength = 0;

        fprintf(stdout, ": ");
        fflush(stdout);

        ssize_t nRead = getline(&tempStr, &userInputLength, stdin);

        // await user input
        if (nRead > 1)
        {
            // successfully read
            if (tempStr[0] == '#')
            {
                // ignore comment
                continue;
            }

            // chop off the newline character
            tempStr[nRead - (size_t)(1)] = '\0';

            // attempt to allocate space for input string
            userInput = calloc(nRead, sizeof(char)); // no +1 since we got rid of the last character
            if (userInput == NULL)
            {
                raise(SIGUSR1);
                return NULL;
            }

            strcpy(userInput, tempStr);
            free(tempStr);

            return userInput;
        }
        else if (nRead < 1)
        {
            fprintf(stdout, "getLine encountered an error\n");
            return NULL;
        }
        else
        {
            // user entered nothing, reprompt
        }
    }
}

/**********************************************************************************************
 * parseToken()
 *
 * Inputs:
 *  UserInputStruct userInput
 *  char* token
 *  size_t* argc
 *
 * Outputs:
 * sets argc to number of arguments parsed
 * parses < , > , & out of the token array and builds the userInput struct
 *
 *********************************************************************************************/
void parseToken(UserInputStruct userInput, char *token, size_t *argc)
{
    // lets take a look at the token
    if (token == NULL)
    {
        return;
    }

    // fprintf(stderr, "Got token: %s\n", token);
    if (strcmp(token, "<") == 0)
    {
        // get next token and set input destination
        token = strtok(NULL, " ");

        strcpy(userInput.inputDestination_ptr, token);

        // get next token and recurse
        token = strtok(NULL, " ");
        parseToken(userInput, token, argc);
    }
    else if (strcmp(token, ">") == 0)
    {
        // get next token and set output destination
        token = strtok(NULL, " ");

        strcpy(userInput.outputDestination_ptr, token);

        // get next token and recurse
        token = strtok(NULL, " ");
        parseToken(userInput, token, argc);
    }
    else if (strcmp(token, "&") == 0)
    {
        // get next token and see if it's NULL, otherwise ignore
        token = strtok(NULL, " ");
        if (token == NULL)
        {
            // User would like to run in background
            if (!fgOnly)
            {
                *userInput.runInBackground = 1;
            }
        }
        else
        {
            // if the next token still contains text, ignore it and continue parsing the line

            parseToken(userInput, token, argc);
        }
    }
    else
    {
        // fprintf(stderr, "Found arg, argc: %zu\n", *argc);
        //  item is command or arg, count it
        *argc = *argc + (size_t)1;

        // get next token and recurse
        token = strtok(NULL, " ");
        parseToken(userInput, token, argc);
    }
}

/**********************************************************************************************
 * getUserInputFromString()
 *
 * Inputs:
 *  char* userInputString
 *
 * Outputs:
 *  Returns a UserInputStruct containing the necessary info to execute a command sent by the user
 *
 *********************************************************************************************/
UserInputStruct getUserInputFromString(char *userInputString)
{

    char *defaultDestination = "/dev/null";

    // initialize the struct
    UserInputStruct userInput;
    userInput.argv = NULL;
    userInput.inputDestination_ptr = NULL;
    userInput.outputDestination_ptr = NULL;
    userInput.runInBackground = NULL;

    userInput.checkSum = malloc(sizeof(int));
    // check to see if checksum allocated
    if(userInput.checkSum == NULL)
    {
        raise(SIGUSR1);
        return userInput;
    }
    else
    {
        *userInput.checkSum = 0;
    }

    // attempt our first allocations
    userInput.inputDestination_ptr = calloc(strlen(defaultDestination) + (size_t)1, (size_t)1);
    if (userInput.inputDestination_ptr == NULL)
    {
        raise(SIGUSR1);
        return userInput;
    }

    userInput.outputDestination_ptr = calloc(strlen(defaultDestination) + (size_t)1, (size_t)1);
    if (userInput.outputDestination_ptr == NULL)
    {
        raise(SIGUSR1);
        return userInput;
    }

    userInput.runInBackground = malloc(sizeof(int));
    if (userInput.runInBackground == NULL)
    {
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
    if (inputString == NULL)
    {
        raise(SIGUSR1);
        return userInput;
    }

    strcpy(inputString, userInputString);

    char *token;
    char *new_token = NULL;
    size_t argc = 0;
    token = strtok(inputString, " ");

    // check to see that we got a non-empty token
    if (token != NULL)
    {
        parseToken(userInput, token, &argc);
    }

    free(inputString);

    userInput.argv = malloc((argc + (size_t)1) * (size_t)sizeof(char *)); // extra item for NULL pointer so we know we've hit the end of the array
    if (userInput.argv == NULL)
    {
        raise(SIGUSR1);
        return userInput;
    }

    // loop through the string again to get the args:
    for (size_t i = 0; i < argc; i++)
    {
        token = NULL;
        if (i == 0)
        {
            // first time through the loop
            token = strtok(userInputString, " ");
        }
        else
        {
            token = strtok(NULL, " ");
        }

        char *moneyPtr = strstr(token, "$$");
        if (moneyPtr != NULL)
        {
            // find how many times we're expanding $$
            int numOccurences = 0;
            for (size_t i = 0; i < strlen(token); i++)
            {
                if (token[i] == '$')
                {
                    if (token[i + (size_t)1] == '$')
                    {
                        numOccurences++;

                        i = i + 1; // consume the second $
                    }
                }
            }

            // do some math to calc new token size
            size_t byteCount = 0;
            pid_t pid = getpid();
            size_t nDigits = floor(log10(abs(pid))) + 1;

            byteCount = (strlen(token) - (2 * numOccurences)) + (nDigits * numOccurences) + 1;
            //(strlen - 2*numoccurences) - string length of original tokens minus however many $ we're removing
            //(nDigits * numOccurences) - digits of the pid * how many times we're inserting it
            //+1 for null terminator

            // convert the pid to a char[]
            char pidChar[nDigits];
            int digit = 0;
            size_t count = 1;
            while (pid > 0)
            {
                digit = pid % 10;
                pid = pid / 10;
                pidChar[nDigits - count] = (char)(digit + 48);
                count++;
            }

            // allocate some space
            new_token = calloc(byteCount, sizeof(char));
            if (new_token == NULL)
            {
                raise(SIGUSR1);
                return userInput;
            }

            size_t offset = 0;
            // build the new token
            for (size_t h = 0; h < strlen(token); h++)
            {
                if (token[h] == '$')
                {
                    if (token[h + (size_t)1] == '$')
                    {
                        // add the pid to the arr
                        for (size_t j = 0; j < nDigits; j++)
                        {
                            new_token[h + j + offset] = pidChar[j];
                        }

                        offset = offset + nDigits - 2;
                        h = h + 1;
                    }
                    else
                    {
                        new_token[h + offset] = '$';
                    }
                }
                else
                {
                    new_token[h + offset] = token[h];
                }
            }
        }

        if (new_token != NULL)
        {
            // expansion perfomed
            userInput.argv[i] = new_token;
        }
        else
        {
            // no $$ expansion performed
            userInput.argv[i] = calloc(strlen(token) + 1, sizeof(char)); // extra space for null terminator, use calloc so that memory space is initialized to \0
            if (userInput.argv[i] == NULL)
            {
                raise(SIGUSR1);
                return userInput;
            }

            // add the token to the array
            strcpy(userInput.argv[i], token);
        }
    }

    // done processing args, append null pointer
    userInput.argv[argc] = (void *)NULL;

    // we made it through without returning early.
    *userInput.checkSum = 1;

    return userInput;
}

/**********************************************************************************************
 * freeUserInput()
 *
 * Purpose: attempts to free up allocated memory.
 *
 * Inputs:
 *  UserInputStruct userInput
 *
 *********************************************************************************************/
void freeUserInput(UserInputStruct userInput)
{
    size_t i = 0;

    // free the argv contents
    while (1)
    {
        if (userInput.argv[i] != NULL)
        {
            free(userInput.argv[i]);
            i++;
        }
        else
        {
            break;
        }
    }

    // free argv itself
    free(userInput.argv);

    // free the others
    free(userInput.inputDestination_ptr);
    free(userInput.outputDestination_ptr);
    free(userInput.runInBackground);

    return;
}

int main()
{
    while (1)
    {
        // register event handlers
        struct sigaction SIGCHLD_action = {{0}};
        sigemptyset(&SIGCHLD_action.sa_mask);
        SIGCHLD_action.sa_sigaction = handle_SIGCHLD;
        SIGCHLD_action.sa_flags = SA_RESTART | SA_SIGINFO;

        struct sigaction SIGINT_action = {{0}};
        sigemptyset(&SIGINT_action.sa_mask);
        SIGINT_action.sa_handler = SIG_IGN;

        struct sigaction SIGTSTP_action = {{0}};
        sigemptyset(&SIGTSTP_action.sa_mask);
        SIGTSTP_action.sa_sigaction = handle_SIGTSTP;
        SIGTSTP_action.sa_flags = SA_RESTART | SA_SIGINFO;

        struct sigaction SIGUSR1_action = {{0}};
        sigemptyset(&SIGUSR1_action.sa_mask);
        SIGUSR1_action.sa_sigaction = handle_SIGUSR1;
        SIGUSR1_action.sa_flags = SA_RESTART | SA_SIGINFO;

        sigaction(SIGCHLD, &SIGCHLD_action, NULL);
        sigaction(SIGINT, &SIGINT_action, NULL);
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);
        sigaction(SIGUSR1, &SIGUSR1_action, NULL);

        // Get input from the user
        char *inputString = NULL;

        inputString = getInputString();
        /*************
         * TODO fix this handling:
         * 
         * //////////*/
        if (inputString == NULL)
        {
            fprintf(stderr, "Failed to get input string \n");
            fflush(NULL);
            free(inputString);
            exit(1);
        }

        // parse the input
        UserInputStruct userInput;
        while (1)
        {
            userInput = getUserInputFromString(inputString);

            // validate the input in the order it was created
            if (userInput.checkSum == NULL)
            {
                fprintf(stderr, "Checksum allocation failed\n");
                fflush(NULL);
            }
            else if (userInput.checkSum == 0)
            {
                fprintf(stderr, "Input allocation failed ");
                if (userInput.inputDestination_ptr == NULL)
                {
                    fprintf(stderr, "...during input destination allocation\n");
                    fflush(NULL);
                }
                else if (userInput.outputDestination_ptr == NULL)
                {
                    fprintf(stderr, "...during output destination allocation\n");
                    fflush(NULL);
                }
                else if (userInput.runInBackground == NULL)
                {
                    fprintf(stderr, "...background boolean allocation\n");
                    fflush(NULL);
                }
                else if (userInput.argv == NULL)
                {
                    fprintf(stderr, "...during pointer array allocation\n");
                    fflush(NULL);
                }
                else
                {
                    free(userInput.checkSum);
                    free(userInput.inputDestination_ptr);
                    free(userInput.outputDestination_ptr);
                    free(userInput.runInBackground);
                    
                    // check arguements.
                    size_t i = 0;
                    // free the argv contents
                    while (1)
                    {
                        if (userInput.argv[i] != NULL)
                        {
                            free(userInput.argv[i]);
                            i++;
                        }
                        else
                        {
                            break;
                        }
                    }

                    free(userInput.argv);
                    fprintf(stderr, "...during an argument allocation\n Read %zu args successfully before error.", i);
                    fflush(NULL);
                }
            } else { 
                break;
            }
        }

        free(inputString);

        // execute the input

        if (strcmp(userInput.argv[0], "cd") == 0)
        {
            if (userInput.argv[1] == NULL)
            {
                if (chdir(getenv("HOME")) != 0)
                {
                    fprintf(stdout, "Encountered an error while attempting to open home directory.\n");
                }
            }
            else
            {
                if (chdir(userInput.argv[1]) != 0)
                {
                    fprintf(stdout, "Directory not found, please try again.\n");
                }
            }
            // command executed ok
        }
        else if (strcmp(userInput.argv[0], "status") == 0)
        {
            if (WIFEXITED(currentStatus))
            {
                fprintf(stdout, "exit value %d\n", WEXITSTATUS(currentStatus));
            }
            else if (WIFSIGNALED(currentStatus))
            {
                fprintf(stdout, "terminated by signal %d\n", WTERMSIG(currentStatus));
            }

            fflush(stdout);
        }
        else if (strcmp(userInput.argv[0], "exit") == 0)
        {
            // kill all processes with SIGTERM
            freeUserInput(userInput);
            kill(0, SIGTERM);
        }
        else
        {
            // else process command for exec
            int childStatus;
            int inputDestination;
            int outputDestination;

            // open redirection destinations
            inputDestination = open(userInput.inputDestination_ptr, O_RDONLY, 0444);
            outputDestination = open(userInput.outputDestination_ptr, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            // Fork a new process
            pid_t spawnPid = fork();

            if (spawnPid < 0)
            {
                fprintf(stderr, "fork(): ");
                freeUserInput(userInput);
                close(inputDestination);
                close(outputDestination);
                exit(1);
            }
            else if (spawnPid == 0)
            {
                /**********************************
                 * CHILD PROCESS
                 *********************************/

                // reregister signal handling
                struct sigaction SIGCHLD_action_child = {{0}};
                sigemptyset(&SIGCHLD_action_child.sa_mask);
                SIGCHLD_action_child.sa_handler = SIG_IGN;

                struct sigaction SIGINT_action_child_fg = {{0}};
                sigemptyset(&SIGINT_action_child_fg.sa_mask);
                SIGINT_action_child_fg.sa_handler = SIG_DFL;

                struct sigaction SIGINT_action_child_bg = {{0}};
                sigemptyset(&SIGINT_action_child_bg.sa_mask);
                SIGINT_action_child_bg.sa_handler = SIG_IGN;

                struct sigaction SIGTSTP_action_child = {{0}};
                sigemptyset(&SIGTSTP_action_child.sa_mask);
                SIGTSTP_action_child.sa_handler = SIG_IGN;

                struct sigaction SIGUSR1_action_child = {{0}};
                sigemptyset(&SIGUSR1_action.sa_mask);
                SIGUSR1_action.sa_handler = SIG_IGN;

                // check to see if they worked
                if (inputDestination < 0)
                {
                    fprintf(stdout, "Can not open file for input\n");
                    freeUserInput(userInput);
                    exit(1);
                }
                if (outputDestination < 0)
                {
                    fprintf(stdout, "Can not open file for output\n");
                    freeUserInput(userInput);
                    exit(1);
                }

                if (*userInput.runInBackground)
                {
                    /**********************************
                     * BACKROUND CHILD
                     *********************************/
                    // stdin/out should be redirected regardless
                    dup2(inputDestination, 0);
                    dup2(outputDestination, 1);

                    // set bg process to ignore SIGINT
                    sigaction(SIGINT, &SIGINT_action_child_bg, NULL);
                }
                else
                {
                    /**********************************
                     * FOREGROUND CHILD
                     *********************************/
                    // only redirect if the user chooses to
                    if (strcmp(userInput.inputDestination_ptr, "/dev/null") != 0)
                    {
                        dup2(inputDestination, 0);
                    }
                    if (strcmp(userInput.outputDestination_ptr, "/dev/null") != 0)
                    {
                        dup2(outputDestination, 1);
                    }

                    sigaction(SIGINT, &SIGINT_action_child_fg, NULL);
                }

                sigaction(SIGTSTP, &SIGTSTP_action_child, NULL);
                sigaction(SIGCHLD, &SIGCHLD_action_child, NULL);
                sigaction(SIGUSR1, &SIGUSR1_action_child, NULL);
                execvp(userInput.argv[0], userInput.argv);
                // exec only returns here if there is an error
                perror("execvp");
                freeUserInput(userInput);
                exit(2);
            }
            else
            {
                /**********************************
                 * PARENT PROCESS
                 *********************************/

                if (*userInput.runInBackground)
                {

                    fprintf(stdout, "Background process PID:(%d)\n", spawnPid);
                    fflush(stdout);
                }
                else
                {
                    struct sigaction temp_act = {{0}};
                    sigemptyset(&temp_act.sa_mask);

                    // temporarily block sigtstp sigchld sigusr1
                    sigaddset(&temp_act.sa_mask, SIGTSTP);
                    sigaddset(&temp_act.sa_mask, SIGCHLD);
                    sigaddset(&temp_act.sa_mask, SIGUSR1);

                    sigprocmask(SIG_BLOCK, &SIGTSTP_action.sa_mask, NULL);

                    spawnPid = waitpid(spawnPid, &childStatus, 0);
                    currentStatus = childStatus;
                    if (WIFSIGNALED(currentStatus))
                    {
                        fprintf(stdout, "terminated by signal %d\n", WTERMSIG(currentStatus));
                    }

                    sigprocmask(SIG_UNBLOCK, &SIGTSTP_action.sa_mask, NULL);
                }
            }

            close(inputDestination);
            close(outputDestination);
        }

        freeUserInput(userInput);
    }
}
