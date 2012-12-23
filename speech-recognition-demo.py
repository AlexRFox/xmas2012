from ispeech import *

try:
    f = open ("apikey", "r")
except:
    print ("scp dellalt:~atw/apikey .")
    exit (1)

key = f.read ().strip ()

speechrecognizer = speechrecognizer()
speechrecognizer.set_parameter("server", "http://api.ispeech.org/api/rest")
speechrecognizer.set_parameter("apikey", key)
speechrecognizer.set_parameter("freeform", "3")
speechrecognizer.set_parameter("locale", "en-US")
speechrecognizer.set_parameter("output", "json")
speechrecognizer.set_parameter("content-type", "mp3")
speechrecognizer.set_parameter("audio", speechrecognizer.base64_encode(speechrecognizer.file_get_contents("output.mp3")))
# speechrecognizer.set_parameter("content-type", "ogg")
# speechrecognizer.set_parameter("audio", speechrecognizer.base64_encode(speechrecognizer.file_get_contents("foo.ogg")))
result = speechrecognizer.make_request()

if isinstance(result, dict): #error occured
    print(result["error"])
else:
    print(result)
