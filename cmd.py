#! /usr/bin/env python3
# powered by iSpeech(R)

from ispeech import *

try:
    f = open ("apikey", "r")
except:
    print ("scp pacew.dyndns.org:~atw/apikey .")
    exit (1)

key = f.read ().strip ()

speechrecognizer = speechrecognizer()
speechrecognizer.set_parameter("server", "http://api.ispeech.org/api/rest")
speechrecognizer.set_parameter("apikey", key)
speechrecognizer.set_parameter("freeform", "0")
speechrecognizer.set_parameter("locale", "en-US")
speechrecognizer.set_parameter("output", "json")
speechrecognizer.set_parameter("content-type", "wav")

#The recognizer will return yes, no, or nothing
speechrecognizer.set_parameter("alias", "command1|YESNO")
speechrecognizer.set_parameter("YESNO", "yes|no")
speechrecognizer.set_parameter("command1", "%YESNO%")

speechrecognizer.set_parameter("audio", speechrecognizer.base64_encode(speechrecognizer.file_get_contents("yes.wav")))
result = speechrecognizer.make_request()

if isinstance(result, dict): #error occured
    print(result["error"])
else:
    print (result);
