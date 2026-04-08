Name: Lucas Myers
Date: 4/1/2026
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

15. Added the Round Robin Scheduling using the ready queue.

16. OSS selects the process at the front of the queue and sends it a message containing the time quantum

17. If the worker uses the whole quantum, it gets placed back into the queue.

18.If it finishes early, it sends a negative value and gets removed from the system.

19. Used message queues to handle communication between oss and worker. (dispatching logic)

20.   Updated the worker so it can simulate amount of CPU usage.

21. Ran into an issue where messages were not being recieved correctly due to wrong type. Fixed this by using PID .

22. Added more detail to logging. Now inlucde ready queues, dispatch events, cput time used by the processes, process termination.

23. Tested program to make sure processes are being scheduled in the correct order.

24. Implemented blocked queue and unblocking logic

25. Added process wake-up handling

26. Created a function to check all blocked processes and determine if they should be unblocked.

27.

28. Improved worker simulation

29. Updated worker to include a 20% chance of blocking.

30 .Improved scheduling realism

31. The system now properly handles all three process states:
Running
Blocked
Terminated

29. Added final CPU utilization report and overall simulation statistics.

30. Added a log line limit so the file does not grow endlessly if something goes wrong.

31. Improved cleanup and signal handling so shared memory, message queues, and child processes are removed correctly on abnormal termination.

32. Finished the blocked and ready queue behavior so the scheduler now handles process creation, dispatching, blocking, unblocking, and termination.

33. Finalized the full scheduler for the project.

34. Added final statistics to the end of the simulation.
The program now reports:
total processes launched
total processes finished
total CPU busy time
total system overhead time
total idle time
average CPU utilization

35. Added a log line limit to prevent the log file from growing too large if the program runs too long or gets stuck in a loop.

36. Improved the final cleanup so child processes, shared memory, and message queues are properly removed on normal exit and abnormal termination.

37. Finished testing different combinations of command line options to make sure the program works correctly with different values for -n, -s, -t, -i, and -f.

38. Final day was mainly used for polishing the project, improving logging, and making sure the scheduler behavior matched the assignment requirements.

39. Added comments for readability.



AI used: ChatGPT

- How to structure the project step by step

This helped break the assignment into smaller parts so I could build it over multiple days instead of all at once.

-Asked ChatGPT how to handle blocking, unblocking, and round-robin scheduling logic.

This helped break down the scheduling portion of the assignment into smaller steps and made it easier to build the project over multiple days.

-Asked ChatGPT how to use signal handling and cleanup functions.

This helped show how to properly remove shared memory, delete message queues, and terminate child processes if the program exits early or gets interrupted.

-Asked ChatGPT how to calculate CPU utilization and improve final statistics.

This helped with adding the final report at the end of the simulation and understanding how to track busy time, idle time, and overhead time.