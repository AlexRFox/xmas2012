#! /usr/bin/env python3
# powered by iSpeech(R)

import sys, re, os
from ispeech import *

if len (sys.argv) <= 1:
    print ("usage: " + sys.argv[0] + " filename [space separated commands]")
    exit (1)

fn = sys.argv[1]
ext = os.path.splitext (fn)[1].lower ()[1:]

# known_fmts = ["aiff", "mp3", "ogg", "wma", "flac",
#               "wav", "alaw", "ulaw", "vox", "mp4"]
known_fmts = ["wav"]

if not ext:
    print ("unable to detect format")
    exit (1)
elif ext not in known_fmts:
    print ("format " + ext + " not supported by ispeech")
    print ("formats supported: " + str (known_fmts))
    exit (1)

cmds = None
if len (sys.argv) >= 3:
    for c in sys.argv[2:]:
        if not re.match ("^[\w\s]+$", c):
            print ("bad command: " + c)
            print ("all commands must match ^[\w\s]+$")
            exit (1)
    cmds = "|".join (sys.argv[2:])

try:
    f = open ("apikey", "r")
except:
    print ("scp pacew.dyndns.org:~atw/apikey .")
    exit (1)

key = f.read ().strip ()

print ("processing " + fn + " as " + ext)

speechrecognizer = speechrecognizer()
speechrecognizer.set_parameter("server", "http://api.ispeech.org/api/rest")
speechrecognizer.set_parameter("apikey", key)
speechrecognizer.set_parameter("locale", "en-US")
speechrecognizer.set_parameter("output", "json")
speechrecognizer.set_parameter("content-type", ext)

if cmds:
    speechrecognizer.set_parameter("freeform", "0")
    speechrecognizer.set_parameter("alias", "command1|CMDS")
    speechrecognizer.set_parameter("CMDS", cmds)
    speechrecognizer.set_parameter("command1", "%CMDS%")
else:
    speechrecognizer.set_parameter("freeform", "3")

speechrecognizer.set_parameter("audio", speechrecognizer.base64_encode(speechrecognizer.file_get_contents(fn)))
result = speechrecognizer.make_request()

if isinstance(result, dict): #error occured
    print(result["error"])
else:
    print (result);
