#!/usr/bin/env python

"""A simple commandclient client around the GMail OAuth API."""

# pip install --user --upgrade google-api-python-client oauth2client
from oauth2client import client
from oauth2client import file
from oauth2client import tools

# You have to Create a project on https://console.developers.google.com/,
# create OAuth client ID credentials, download them and put them in
# client_id.json.
clientsecrets = 'client_id.json'
flow = client.flow_from_clientsecrets(clientsecrets,
    scope='https://www.googleapis.com/auth/gmail.send',
    message=tools.message_if_missing(clientsecrets))

storage = file.Storage('mail.dat')
creds = storage.get()
if creds is None or creds.invalid:
  #args = tools.argparser.parse_args(['--noauth_local_webserver'])
  args = tools.argparser.parse_args()
  creds = tools.run_flow(flow, storage, args)

from googleapiclient.discovery import build
service = build('gmail', 'v1', credentials=creds)

# https://developers.google.com/gmail/api/guides/sending
import base64
from email.mime.text import MIMEText
import argparse
import sys
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--subject')
parser.add_argument('--to', required=True)
args = parser.parse_args()

message = MIMEText(sys.stdin.read())
message['to'] = args.to
message['subject'] = args.subject
message = {'raw': base64.urlsafe_b64encode(message.as_string())}

service.users().messages().send(userId='me', body=message).execute()
