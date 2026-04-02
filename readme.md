Name: Lucas Myers
Date: 4/7/2026
Environment: Visual Studio Code, Linux

How to Compile Project: 
Type 'make'

Example of how to run the project:

From the project directory:

make clean
make
./oss -n 5 -s 2 -t 3 -i 0.5

1. Started the project by setting up the skeleton for oss and worker programs along with the basic structure.

2. Created the shared memory for the simulated sys clock using shmget() and attached it using shmat() so both oss and worker can access it.

3. Initialized the simulated clock (seconds and nanoseconds) and created a function to increment the clock properly while handling nanosecond overflow.

4. Built the Process Control Block structure and initialized a process table array to track processes.

5. Used getopt() to handle command line arguments


6. Used atoi() and atof() to convert inputs into integers and doubles so they can be used in the program.

7. Added input validation to ensure values are not negative and stay within allowed limits.

8. Created the message queue using msgget() to allow communication between oss and workers.

9. Implemented signal handling SIGINT, SIGTERM, SIGALRM to clean up shared memory and message queues if the program is terminated early.

10. Created a cleanup function to remove shared memory and message queues using shmctl() and msgctl().

11. Set up logging using an output file to track system activity and print the process table.

12. Built the Makefile .

13. Tested that oss runs correctly, initializes all structures, writes to the log file, and exits cleanly without memory or IPC leaks.

14. Worker process currently acts as a placeholder to ensure compilation and message queue setup works before adding scheduling logic.

AI used: ChatGPT

- How to structure the project step by step

This helped break the assignment into smaller parts so I could build it over multiple days instead of all at once.