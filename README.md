
      .-')   _   .-')      ('-.                          .-')    ('-. .-.
     ( OO ).( '.( OO )_   ( OO ).-.                     ( OO ). ( OO )  /
    (_)---\_),--.   ,--.) / . --. / ,--.      ,--.     (_)---\_),--. ,--.
    /    _ | |   `.'   |  | \-.  \  |  |.-')  |  |.-') /    _ | |  | |  |
    \  :` `. |         |.-'-'  |  | |  | OO ) |  | OO )\  :` `. |   .|  |
     '..`''.)|  |'.'|  | \| |_.'  | |  |`-' | |  |`-' | '..`''.)|       |
    .-._)   \|  |   |  |  |  .-.  |(|  '---.'(|  '---.'.-._)   \|  .-.  |
    \       /|  |   |  |  |  | |  | |      |  |      | \       /|  | |  |
     `-----' `--'   `--'  `--' `--' `------'  `------'  `-----' `--' `--'
 Author: Joe Maurer

 Description: This is a shell program, written in C. Contains built in
 support for 3 commands:

  cd          - change directory
  status      - provides the exit status of the program, or last child if any
                have been terminated.
  exit/quit   - terminates the shell program and any child processes (hotkey:
 ctrl^\) foreground and background.

 Any other commands are handled by a call to an exec() function with provided
 arguments. Shell supports input/output redirection as well as optional
 background execution (&).

 Command syntax:

  command [arg1 arg2 ...] [< input_file] [> output_file] [&]

 Instructions:

 Compile with
  gcc -std=c99 -Wall -g -o smallsh smallsh.c -lm