#!/usr/bin/env python
from bs4 import BeautifulSoup
import requests
import sys

# A simple scraper with bs4 and requests.

ROOT = \
    'http://lang-8.com/609963/journals/159998518702980022315861616374779453028'

count = 0
all_contents = BeautifulSoup()
link = ROOT
while link:
    count += 1
    print >> sys.stderr, count
    response = requests.get(link)
    soup = BeautifulSoup(response.text)
    contents = soup.find(id='body_show_ori').contents
    try:
        i = contents.index(u'next : ')
        link = contents[i + 1]['href']
        all_contents.contents += contents[:i-1]  # strip one <br/>
    except:
        link = None

print """\
<meta encoding="utf-8">
<style>
  body { width: 600px; margin: 30px auto; }
</style>"""
print all_contents.prettify().encode('utf-8')
