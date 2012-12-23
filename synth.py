#! /usr/bin/env python3

from ispeech import *
import sqlite3, time, re, os, getopt, sys

if len (sys.argv) <= 1:
    print ("usage: synth.py words to speak")
    exit (1)

tokens = sys.argv[1:]
phrase = " ".join (tokens)

home = os.environ['HOME']

db = home + "/xmas2012/speech.sqlite"
if not os.path.isfile (db):
    print ("speech.sqlite doesn't exist\n")
    print ("sudo apt-get install sqlite3")
    print ("echo 'CREATE TABLE speech (phrase text, filename text);' | sqlite3 " + db)
    exit (1)

conn = sqlite3.connect (db)

c = conn.cursor ()

t = (phrase,)
c.execute ("select * from speech where phrase=? collate nocase", t)
r = c.fetchone ()
if r:
    print (r[1])
    conn.close ()
    exit ()

try:
    f = open ("apikey", "r")
except:
    print ("scp dellalt:~atw/apikey .")
    exit (1)

key = f.read ().strip ()

speechsynthesizer = speechsynthesizer()
speechsynthesizer.set_parameter("server", "http://api.ispeech.org/api/rest")
speechsynthesizer.set_parameter("apikey", key)
speechsynthesizer.set_parameter("text", phrase)
speechsynthesizer.set_parameter("format", "ogg")
speechsynthesizer.set_parameter("voice", "usenglishfemale")
speechsynthesizer.set_parameter("output", "json")
result = speechsynthesizer.make_request()

if isinstance(result, dict): #error occured
    print(result["error"])
    conn.close ()
    exit (1)

fn = ""
for i in range (min (len (tokens), 2)):
    fn += re.sub (r"[^a-z]", "", tokens[i].lower ()) + "-"
fn += str (int (time.time ())) + ".mp3"

absfn = home + "/xmas2012/speech/" + fn

r = speechsynthesizer.file_put_contents(absfn, result)
if not r:
    conn.close ()
    exit (1)

print (absfn)

t = (phrase, absfn)
c.execute ("insert into speech values (?, ?)", t)
conn.commit ()
conn.close ()
