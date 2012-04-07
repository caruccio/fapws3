#pragma once

#include "common.h"

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#include <Python.h>

/*
Structure we use for each client's connection. 
*/
struct client
{
	long id; //py_client.id()
	ev_io ev_write;
	ev_io ev_read;
	int fd;
	char *remote_addr;
	int remote_port;
	char *input_header;
	char *input_body;
	size_t input_pos;
	int retry;
	char *uri;
	char *cmd;
	char *uri_path;   //correspond to the registered uri_header 
	PyObject *wsgi_cb;
	int response_iter_sent; //-2: nothing sent, -1: header sent, 0-9999: iter sent
	char response_header[MAXHEADER];
	int response_header_length;
	PyObject *pyenviron;
	PyObject *pystart_response;
	PyObject *response_content;
	PyObject *response_content_obj;
	FILE *response_fp; // file of the sent file
	struct TimerObj tout;
	PyObject* py_client; //python instance of class Client
};

int init_client(void);
void terminate_client(void);
struct client* set_current_client(struct client* cli);
struct client* get_current_client(void);
int register_client(struct client *cli);
void unregister_client(struct client *cli);
struct client *get_client(PyObject *py_client);
int set_client_timer(struct client* cli, float timeout, PyObject* py_cb);
long get_py_client_id(PyObject *py_client);
