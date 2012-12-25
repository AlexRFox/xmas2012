#! /usr/bin/env python

import subprocess

print "Opening subprocess"

frotz = subprocess.Popen (["/home/eric/bin/dfrotz", "advent.z5"],
                          stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE)

print "Reading first line"

print frotz.stdout.readline()
print ">,"
frotz.stdin.write ("look\n")
print frotz.stdout.readline()

frotz.stdin.write ("quit\n")
print frotz.stdout.readline()

stdout, stderr = frotz.communicate ("yes")

print stdout

frotz.terminate ()
