#!/usr/bin/python

import os, signal, time
from itertools import takewhile
import fapws._evwsgi as evwsgi
from fapws import base
import urlparse

try:
	import posh
	use_posh = True
except:
	use_posh = True

class HttpStatus:
	status = {
		# 2xx Success
		200: 'OK',
		201: 'Created',
		202: 'Accepted',
		203: 'Partial Information',
		204: 'No Content',
		# 3xx Redirection
		301: 'Moved',
		302: 'Found',
		303: 'Method',
		304: 'Not Modified',
		# 4xx 5xx Error
		400: 'Bad request',
		401: 'Unauthorized',
		402: 'Payment Required',
		403: 'Forbidden',
		404: 'Not found',
		500: 'Internal Error',
		501: 'Not implemented',
		502: 'Service temporarily overloaded',
		503: 'Gateway timeout',
	}

	def __init__(self, code):
		print '+ HttpStatus %i' % code
		if HttpStatus.status.has_key(code):
			self.code = code
			self.status = '%i %s' % (code, self.status[code])
		else:
			self.code = 500
			self.status = '%i %s' % (self.code, self.status[self.code])

	def __str__(self):
		return self.status

#######################################################
# Message is a free string with some identifiers
#######################################################
class Message:
	def __init__(self, mid, timestamp, content):
		self.mid = mid
		self.timestamp = timestamp
		self.content = content
		print '+ Message %i = %s' % (mid, self)

	def __repr__(self):
		return str(self)

	def eq(self, other):
		return self.mid == other.mid

	def __str__(self):
		#[{"version":"1","operation":"INSERT","channelCode":"teste_17082011","reference":"0","payload":"Greetings from Porto Alegre, RS.","realtimeId":"2","dtCreated":"1314721007"},0]
		return self.content

#######################################################
# MessageStream is a helper to organize messages from a
# Channel. Every new message has an index (mid) and a value.
# If new mid is greater than last mid, all positions up
# to mid are filled with None,  i.e. a MessageStream
# always has a number of elements equals to the mid of
# the greatest message, even if that is the only valid
# message stred.
# This makes easy to find "holes" in the stream. Dict()s
# are not order safe.
#######################################################
class MessageStream(list):
	def insert(self, message):
		if message.mid == len(self):
			# this is the next message
			self.append(message)
		elif message.mid < len(self):
			#overwrite existing position
			self[message.mid] = message
		else:
			# message is way too far from last message. fill with None and append
			self.extend([ None for x in range(message.mid - len(self)) ])
			self.append(message)
		print 'insert:', self

		#print '+ MessageStream: %s' % self

	def has(self, mid):
		return mid < len(self) # and self[mid] != None

	def range(self, start, count=1):
		'''Return "count" messages starting from "start"'''
		return [ x for x in takewhile(lambda x: x != None, self[start:start + count]) ]

	def front(self, count=1):
		'''Return first "count" messages'''
		return [ x for x in takewhile(lambda x: x != None, self[0:count]) ]

	def back(self, count=1):
		'''Return last "count" messages'''
		l = [ x for x in takewhile(lambda x: x != None, reversed(self[len(self) - count:])) ]
		l.reverse()
		return l

	## some silly precautions
	def pop(self, *vargs):
		raise NotImplementedError('MessageStream is inmutable')

	def remove(self, *vargs):
		raise NotImplementedError('MessageStream is inmutable')

	def sort(self, *vargs):
		raise NotImplementedError('MessageStream is inmutable')

	def reverse(self, *vargs):
		raise NotImplementedError('MessageStream is inmutable')

#######################################################
# Channel is an aggregator for subscribers and messages
#######################################################
class Channel:
	def_timeout = 3

	def __init__(self, name, timeout):
		self.name = name
		self.subs = dict() # Subscribers for this channel
		self.stream = MessageStream()
		self.timeout = timeout
		print '+ Channel %s' % self

	def __len__(self):
		return len(self.stream)

	def get_message(self, mid):
		'''Retrieve a single message'''
		print 'Channel> get_message %s' % mid
		if self.stream.has(mid):
			m = self.stream[mid]
			if m:
				return [m, 0]
			else:
				raise HttpStatus(204)
		else:
			return None

	def get_message_range(self, mid, count=-1):
		'''Retrieve all messages starting from mid, up to count. If count is -1, retrieve up to end.
		If a hole is found, retrieve up to hole - 1.'''
		print 'Channel> get_message_range %s:%i' % (mid, count)
		if self.stream.has(mid):
			m = self.stream.range(mid, count)
			if m:
				return m + [0]
			else:
				raise HttpStatus(204)
		else:
			return None

	def get_message_all(self):
		return self.stream.front(len(self.stream)) + [0]

	def get_message_oldest(self, count=10):
		'''Retrieve count oldest messages. If a hole is found, retrieve up to that.'''
		print 'Channel> get_message_oldest %i' % count
		return self.stream.front(count) + [0]

	def get_message_newest(self, count=10):
		'''Retrieve count newest messages. If a hole is found, retrieve up to that.'''
		print 'Channel> get_message_newest %i' % count
		return self.stream.back(count) + [0]

	def subscribe(self, client):
		print 'Channel> subscribe %s' % client
		self.subs[len(self.subs)] = client
		evwsgi.register_client(client)

	def send_message(self, client, message):
		print 'Channel> send_message %s - %s' % (client, message)
		client.start_response('200 OK', [('Content-Type','application/json; charset="ISO-8859-1"')])
		evwsgi.write_response(client, [str(message)])

	def send_error(self, client, http_error):
		print 'Channel> send_error %s - %i' % (client, http_error.code)
		client.start_response(http_error.status, [('Content-Type','text/plain; charset="ISO-8859-1"')])
		evwsgi.write_response(client, [http_error.status])

	def publish(self, message):
		print 'Channel> publish %s' % message.mid
		self.stream.insert(message)

		# broadcast to subscribers
		for n, client in self.subs.items():
			try:
				#TODO: check if client is waiting for this specific message
				self.send_message(client, self.get_message(message.mid))
			except HttpStatus, ex:
				self.send_error(client, ex)

		# unregister subscribers
		total = len(self.subs)
		self.subs.clear()
		return total

	#def expire(self, now):
	#	for n, client in self.subs.items():
	#		if now >= client.start + self.timeout:
	#			print 'Channel> expire %s' % client
	#			self.send_error(client, HttpStatus(503))
	#			del self.subs[n]

#######################################################
# ChannelPool handles an arbitrary number of Channels
#######################################################
class ChannelPool:

	def __init__(self):
		self.pool = dict()
		print '+ ChannelPool %s' % self

	def create_channel(self, name, timeout=Channel.def_timeout):
		print 'ChannelPool> create_channel %s' % name
		try:
			return self.pool[name]
		except:
			ch = Channel(name, timeout)
			self.pool[name] = ch
			return ch

	def get_channel(self, name):
		print 'ChannelPool> get_channel %s' % name
		try:
			return self.pool[name]
		except:
			return None

	#def expire(self, now):
	#	for name, channel in self.pool.items():
	#		channel.expire(now)

cpool = ChannelPool()

#######################################################
# this is our 'main loop", where we handle client
# requests and do our publisher/subscriber logic
#######################################################
def start(no=0, shared=None):

	if shared and use_posh == True:
		print 'child[%i]: %i' % (no, posh.getpid())

	#def on_interrupt(signum, frame):
	#	print 'Child %i received SIGINT. Exiting...' % no
	#	posh.exit(0)
	#signal.signal(signal.SIGINT, on_interrupt)

	def return_mesgs(mesgs, ch, environ, start_response):
		if mesgs:
			start_response('200 OK', [('Content-Type','application/json; charset="ISO-8859-1"')])
			return [str(mesgs)]
		else:
			# mesg not found, subscribe this client
			ch.subscribe(base.Client(environ, start_response))
			return True

	# Subscriber handler
	def subscribe(environ, start_response):
		# Req:
		# GET /broadcast/sub/ch=teste_17082011&m=2&s=M HTTP/1.1
		# Res:
		#[{
		#	"version": "1",
		#	"operation":"INSERT",
		#	"channelCode":"teste_17082011",
		#	"reference":"0",
		#	"payload":"Greetings from Porto Alegre, RS.",
		#	"realtimeId":"2",
		#	"dtCreated":"1314721007"
		#}, 0]

		print '######## start subscriber ########'
		try:
			qs = urlparse.parse_qs(environ['QUERY_STRING'])

			# find channel
			_ch = qs['ch'][0]
			ch = cpool.get_channel(_ch)
			if not ch:
				start_response('404 Not Found', [('Content-Type','text/plain')])
				return ['invalid channel %s' % _ch]

			# find message
			_s = qs['s'][0]
			if _s == 'M':
				mesg = ch.get_message(int(qs['m'][0]))
				if mesg:
					# got it! send it now
					#ch.send_message(Client(environ, start_response), mesg)
					start_response('200 OK', [('Content-Type','application/json; charset="ISO-8859-1"')])
					return return_mesgs(mesg, ch, environ, start_response)
				else:
					# mesg not found, subscribe this client
					print 'Go subscribe...'
					ch.subscribe(base.Client(environ, start_response, timeout_cb=do_timeout, timeout=Channel.def_timeout))
					return True
			elif _s == 'F':
				return return_mesgs(ch.get_message_oldest(int(qs['m'][0])), ch, environ, start_response)
			elif _s == 'L':
				return return_mesgs(ch.get_message_newest(int(qs['m'][0])), ch, environ, start_response)
			elif _s == 'A':
				return return_mesgs(ch.get_message_all(), ch, environ, start_response)
			else:
				start_response('400 Bad request', [('Content-Type','text/plain')])
				return ['invalid parameter: s=%s' % _s]
		except HttpStatus, ex:
			start_response('%s' % ex.status, [('Content-Type','text/plain')])
			return ['invalid parameter: %s' % ex]
		except Exception, ex:
			start_response('400 Bad request', [('Content-Type','text/plain')])
			return ['invalid parameter: %s' % ex]
			

#		print 'python: on_get', client
		
		#if len(queue) % 100 == 0:
		#	print len(queue), 'clients'

		# by returning True we signal write_cb() this is not a ordinary connection.
		# instead, it will store struct client* and 'block' the socket, waiting for
		# an event to unblock and feed it.
		#return True

	# Publisher handler
	def publish(environ, start_response):

		# example message
		#{
		#	'fapws.params': {
		#		'rt_message': ['{"version":"1","operation":"INSERT","channelCode":"ch_teste","reference":"0","payload":"This is Spartaaaaaaa!!!","realtimeId":"1","dtCreated":"1314721709"},']
		#	},
		#	'SERVER_PORT': '8080',
		#	'CONTENT_TYPE': 'application/x-www-form-urlencoded',
		#	'fapws.uri': '/broadcast/pub?ch=ch_teste&m=ch_teste&t=1314721709',
		#	'SERVER_PROTOCOL': 'HTTP/1.1',
		#	'fapws.raw_header': 'POST /broadcast/pub?ch=ch_teste&m=ch_teste&t=1314721709 HTTP/1.1\r\nUser-Agent: curl/7.21.3 (i686-pc-linux-gnu) libcurl/7.21.3 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18\r\nHost: localhost:8080\r\nAccept: */*\r\nContent-Length: 167\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\nrt_message={"version":"1","operation":"INSERT","channelCode":"ch_teste","reference":"0","payload":"This is Spartaaaaaaa!!!","realtimeId":"1","dtCreated":"1314721709"},',
		#	'SCRIPT_NAME': '/broadcast/pub',
		#	'wsgi.input': <cStringIO.StringO object at 0xb7741a60>,
		#	'REQUEST_METHOD': 'POST',
		#	'HTTP_HOST': 'localhost:8080',
		#	'PATH_INFO': '',
		#	'wsgi.multithread': False,
		#	'QUERY_STRING': 'ch=ch_teste&m=ch_teste&t=1314721709',
		#	'HTTP_CONTENT_TYPE': 'application/x-www-form-urlencoded',
		#	'REQUEST_PROTOCOL': 'HTTP/1.1',
		#	'HTTP_ACCEPT': '*/*',
		#	'fapws.remote_port': 52908,
		#	'HTTP_USER_AGENT': 'curl/7.21.3 (i686-pc-linux-gnu) libcurl/7.21.3 OpenSSL/0.9.8o zlib/1.2.3.4 libidn/1.18',
		#	'wsgi.version': (1, 0),
		#	'fapws.major_minor': '1.1',
		#	'SERVER_NAME': '0.0.0.0',
		#	'REMOTE_ADDR': '127.0.0.1',
		#	'wsgi.run_once': False,
		#	'wsgi.errors': <cStringIO.StringO object at 0xb7741a40>,
		#	'wsgi.multiprocess': True,
		#	'wsgi.url_scheme': 'HTTP',
		#	'fapws.remote_addr': '127.0.0.1',
		#	'HTTP_CONTENT_LENGTH': '167',
		#	'CONTENT_LENGTH': 167
		#}

		print '######## start publisher ########'

		if environ['REQUEST_METHOD'] != 'POST':
			start_response('400 Bad request', [('Content-Type','text/plain')])
			return ['invalid method. Expected POST.']

		try:
			#print environ['rt_message']
			#qs = urlparse.parse_qs(environ['fapws.params'])
			qs = environ['fapws.params']
			_ch = qs['ch'][0]           # channel name
			_m = int(qs['m'][0])        # message id
			_t = qs['t'][0]             # fetch mode
			try:
				_to = float(qs['to'][0]) # client timeout
			except:
				_to = ChannelPool.def_timeout
			_mesg = environ['fapws.params']['rt_message'][0]
			mesg = Message(_m, _t, _mesg)
			ch = cpool.get_channel(_ch)
			if not ch:
				ch = cpool.create_channel(_ch, _to)
		except KeyError, ex:
			start_response('400 Bad request', [('Content-Type','text/plain')])
			return ['invalid parameter: %s' % ex]

		# for each subscriber we saved in on_get(), schedule a write.
		subs_count = ch.publish(mesg)

		# response to publisher
		start_response('200 OK', [('Content-Type','text/plain')])
		return ['queued messages: %i\nlast requested: %i sec. ago (-1=never)\nactive subscribers: %i\n' % (len(ch), -1, subs_count) ] #TODO

	#def clock():
	#	cpool.expire(time.time())

	def do_timeout(client):
		print 'python client timeout', client
		#evwsgi.close_client(client)

	evwsgi.wsgi_cb(('/broadcast/sub', subscribe))
	evwsgi.wsgi_cb(('/broadcast/pub', publish))

	evwsgi.run()

######################
# ignore this for now
######################
def fork_main(child_no=1):
	print 'parent:', os.getpid()

	def create_shared():
		return {'name': 'realtime'}

	channels = create_shared()
	posh.share(channels)

	evwsgi.start('0.0.0.0', '8080')
	evwsgi.set_base_module(base)

	child = list()
	for i in range(child_no):
		child.append(posh.forkcall(start, i, channels))

	def on_interrupt(signum, frame):
		print 'terminating %i children' % len(child)
		for pid in child:
			print 'kill(%i)' % pid
			os.kill(pid, signal.SIGINT)
	signal.signal(signal.SIGINT, on_interrupt)

	print 'forked %i childs' % len(child)
	posh.waitall()

#############################
# start WSGI
#############################
def main():
	evwsgi.set_log_level(7)
	evwsgi.start('0.0.0.0', '8080')
	evwsgi.set_base_module(base)
	start()

#############################
# unittests
#############################
def unittest():
	print 'test Message'
	assert Message(0,0,'').eq(Message(0,0,''))
	assert Message(0,1,'').eq(Message(0,0,''))
	assert Message(0,0,'wasabi').eq(Message(0,0,'shoyu'))

	print 'test MessageStream'
	s = MessageStream()
	tpl = [ Message(i, 0, 'text-%i' % i) for i in range(10) ]

	print 'test MessageStream.insert'
	[ s.insert(i) for i in tpl ]

	print 'test MessageStream.front'
	assert len(s.front()) == 1
	assert len(s.front(5)) == 5
	f = s.front(3)
	assert f[0] == tpl[0] and f[1] == tpl[1] and f[2] == tpl[2]

	print 'test MessageStream.back'
	assert len(s.back()) == 1
	assert len(s.back(4)) == 4
	f = s.back(3)
	assert f[0] == tpl[7] and f[1] == tpl[8] and f[2] == tpl[9]

	print 'test MessageStream holes'
	tpl = [ Message(0, 0, 'text-0'), Message(1, 1, 'text-1'), Message(2, 2, 'text-2'), Message(4, 4, 'text-4'), Message(5, 5, 'text-5'), Message(7, 7, 'text-7') ]
	s = MessageStream()
	[ s.insert(i) for i in tpl ]
	assert len(s.front(7)) == 3
	assert len(s.back(7)) == 1

#############################
# main
#############################
if __name__ == '__main__':
	import sys

	if len(sys.argv) > 1 and sys.argv[1] == '--unittest':
		unittest()
		sys.exit(0)

	main()
#	import sys
#	if use_posh == True:
#		print 'install posh module to enable shared objects between child process: http://poshmodule.sourceforge.net/'
#		fork_main()
#	elif len(sys.argv) > 1 and sys.argv[1] == '-f':
#		fork_main()
#	else:
#		main()

