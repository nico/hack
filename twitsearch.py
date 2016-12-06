consumer, secret = open('twitsearch_keys.txt').read().splitlines()

# https://dev.twitter.com/oauth/application-only
# The base64 stuff described there is the normal Basic Auth dance.

import requests
r = requests.post('https://api.twitter.com/oauth2/token',
                  auth=(consumer, secret),
                  headers={'Content-Type':
                      'application/x-www-form-urlencoded;charset=UTF-8'},
                  data='grant_type=client_credentials')
#print r.headers
#print r.json()
assert r.json()['token_type'] == 'bearer'
bearer = r.json()['access_token']

ratelimit_url = 'https://api.twitter.com/1.1/application/rate_limit_status.json'
r = requests.get(ratelimit_url,
                 headers={'Authorization': 'Bearer ' + bearer},
                 params={'resources': 'search,application'})

#print r.json()
search_rate = r.json()['resources']['search']['/search/tweets']
print 'search limit/15 min:', search_rate['limit']
print 'search remaining/15 min:', search_rate['remaining']

#url = 'https://api.twitter.com/1.1/search/tweets.json?q=%23foo'
#r = requests.get(url, headers={'Authorization': 'Bearer ' + bearer})
#print r.json()
