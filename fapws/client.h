#pragma once

#include "common.h"
#include <Python.h>

/*
Structure we use for each client's connection. 
*/
struct client
{
	int fd;
	ev_io ev_write;
	ev_io ev_read;
	ev_io ev_close;
	char *remote_addr;
	int remote_port;
	char *input_header;
	char *input_body;
	size_t input_pos;
	int retry;
	char *uri;
	char *cmd;
	char *http_major_minor;
	char *uri_path;   //correspond to the registered uri_header 
	PyObject *wsgi_cb;
	int response_iter_sent; //-2: nothing sent, -1: header sent, 0-9999: iter sent
	char response_header[MAXHEADER];
	int response_header_length;
	PyObject *response_content;
	PyObject *response_content_obj;
	FILE *response_fp; // file of the sent file
	struct TimerObj tout;
	PyObject* py_client; //python instance of class Client
};

struct client* set_current_client(struct client* cli);
struct client* get_current_client(void);
int register_client(struct client *cli);
void unregister_client(PyObject *py_client);
struct client* get_client(PyObject *py_client);
void close_client(PyObject* py_client);
int set_client_timer(struct client* cli, float timeout, PyObject* py_cb);
