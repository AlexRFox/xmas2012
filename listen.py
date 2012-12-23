#! /usr/bin/env python3
# powered by iSpeech(R)

import sys
from ispeech import *

if len (sys.argv) != 2:
    print ("usage: listen filename")
    exit (1)

fn = sys.argv[1]

try:
    f = open ("apikey", "r")
except:
    print ("scp pacew.dyndns.org:~atw/apikey .")
    exit (1)

key = f.read ().strip ()

speechrecognizer = speechrecognizer()
speechrecognizer.set_parameter("server", "http://api.ispeech.org/api/rest")
speechrecognizer.set_parameter("apikey", key)
speechrecognizer.set_parameter("freeform", "3")
speechrecognizer.set_parameter("locale", "en-US")
speechrecognizer.set_parameter("output", "json")
speechrecognizer.set_parameter("content-type", "wav")
speechrecognizer.set_parameter("audio", speechrecognizer.base64_encode(speechrecognizer.file_get_contents(fn)))
result = speechrecognizer.make_request()

if isinstance(result, dict): #error occured
    print(result["error"])
else:
    print(result)
