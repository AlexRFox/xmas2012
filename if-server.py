#! /usr/bin/env python

import os, sys

def get_output (fd):
    print "Getting output from fd."
    output = os.read (fd, 1024)
    print "Got " + str(len(output)) + " bytes."
    if output == "":
        print "\nDone."
        exit(0)
    return output

def main ():
    commands = ["look", "inventory", "east", "take all", "inventory", "quit",
                "yes"]

    stdin  = sys.stdin.fileno() # usually 0
    stdout = sys.stdout.fileno() # usually 1

    from_child, child_stdout  = os.pipe() 
    child_stdin,  to_child = os.pipe() 

    print "Pipes constructed. Forking."

    pid = os.fork ()

    if pid: # Parent process
        print "I am the parent."
        os.close (child_stdout)
        os.close (child_stdin)

        print "Child ends closed."

        print "Pipes duplicated. Looping."

        try:
            while True:
                output = get_output (from_child)
                print output
                while len(output) == 1024:
                    output = get_output (from_child)
                    print output
#                line = raw_input ()
                if len(commands) > 0:
                    line = commands.pop(0)
                    print line
                    os.write (to_child, line + "\n")
                else:
                    print "\nDone."
                    exit(0)
        except (KeyboardInterrupt, EOFError):
            print "\nDone."
            exit(0)

    else: # Child process

        print "I am the child."

        os.close(from_child)
        os.close(to_child)
        
        print "Parent ends closed."

        os.dup2(child_stdin,  stdin)
        os.dup2(child_stdout, stdout)

        print "Pipes duplicated. Exec'ing"

        os.execlp ("dfrotz", "/usr/local/games/dfrotz", "advent.z5")

if __name__ == "__main__":
    main ()
