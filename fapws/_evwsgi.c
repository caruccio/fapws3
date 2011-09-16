// vim: ts=4
/*
    Copyright (C) 2009 William.os4y@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


*/

#include <fcntl.h>   //for setnonblocking 
#include <stddef.h>  //for the offset command

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <assert.h>

#include <ev.h>

#include <Python.h>
#include "extra.h"
#include "common.h"
#include "client.h"
#include "mainloop.h"
#include "wsgi.h"

/*
Somme  global variables
*/
int log_level = LOG_ERR; //from syslog.h
char *server_name="127.0.0.1";
char *server_port="8000";
int sockfd;  // main sock_fd
char *VERSION;
#define BACKLOG 1024     // how many pending connections queue will hold
char *date_format;


PyObject *py_base_module;  //to store the fapws.base python module
PyObject *py_config_module; //to store the fapws.config module
PyObject *py_registered_uri; //list containing the uri registered and their associated wsgi callback.
PyObject *py_generic_cb=NULL; 
#define MAX_TIMERS 10 //maximum number of running timers
struct TimerObj *list_timers[MAX_TIMERS];
int list_timers_i=0; //number of values entered in the array list_timers
struct ev_loop *loop; // we define a global loop
PyObject *pydeferqueue;  //initialisation of defer
ev_idle *idle_watcher;
static PyObject *ServerError;

/*
Procedure exposed in Python will establish the socket and the connection.
*/
static PyObject *py_ev_start(PyObject *self, PyObject *args)
{
/*
code copied from the nice book written by Bee:
http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
*/
    struct addrinfo hints, *servinfo, *p;
//    struct sockaddr_storage their_addr; // connector's address information
    int yes=1;
    int rv;

    if (!PyArg_ParseTuple(args, "ss", &server_name, &server_port))
    {
        PyErr_SetString(ServerError, "Failed to parse the start parameters. Must be 2 strings.");
        return NULL;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(server_name, server_port, &hints, &servinfo)) == -1) {
        PyErr_Format(ServerError, "getaddrinfo: %s", gai_strerror(rv));
        return NULL;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        PyErr_SetString(ServerError, "server: failed to bind");
        return NULL;
    }

    if (listen(sockfd, BACKLOG) == -1) {
        PyErr_SetString(ServerError, "listen");
        return NULL;
    }
	LINFO("listen on %s:%s", server_name, server_port);
    return Py_None;
}

void start_timer(struct TimerObj *timer, void* cb)
{
	LDEBUG("new timer: t=%f r=%f cb=%p", timer->timeout, timer->repeat, cb);
	ev_timer_init(&timer->timerwatcher, cb, timer->timeout, timer->repeat);
	ev_timer_start(loop, &timer->timerwatcher);
}

/*
Procedure exposed in Python will generate and start the event loop
*/
static PyObject *py_run_loop(PyObject *self, PyObject *args)
{
    char *backend="";
    int i;
    ev_io accept_watcher;
    ev_signal signal_watcher, signal_watcher2, signal_watcher3;
    struct TimerObj *timer;
    loop = ev_default_loop (0);
    switch (ev_backend(loop))
    {
        case 1:
            backend="select";
            break;
        case 2:
            backend="poll";
            break;
        case 4:
            backend="epoll";
            break;
        case 8:
            backend="kqueue";
            break;
    }
    LINFO("Using %s as event backend", backend);
    ev_io_init(&accept_watcher,accept_cb,sockfd,EV_READ);
    ev_io_start(loop,&accept_watcher);
    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);
    ev_signal_init(&signal_watcher2, sigpipe_cb, SIGPIPE);
    ev_signal_start(loop, &signal_watcher2);
    ev_signal_init(&signal_watcher3, sigterm_cb, SIGTERM);
    ev_signal_start(loop, &signal_watcher3);
    idle_watcher = malloc(sizeof(ev_idle));
    ev_idle_init(idle_watcher, idle_cb);
    if (list_timers_i>=0)
    {
        for (i=0; i<list_timers_i; i++)
        {
            timer=list_timers[i];
				start_timer(timer, timer_cb);
        }
    }
    ev_loop (loop, 0);
    return Py_None;
}

/*
Procedure exposed in Python to provide libev's ABI version
*/
static PyObject *py_libev_version(PyObject *self, PyObject *args)
{
    PyObject *pyres=Py_BuildValue("ii", ev_version_major(), ev_version_minor());
    return pyres;
}



/*
Procedure exposed in Python to register the "base" module
*/
static PyObject *py_set_base_module(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, "O", &py_base_module)) 
        return NULL;
    py_config_module=PyObject_GetAttrString(py_base_module, "config");
    py_registered_uri = PyList_New(0);

    //Get the version from the config.py file
    PyObject *pyver=PyObject_GetAttrString(py_config_module,"SERVER_IDENT");
    VERSION=PyString_AsString(pyver);
    
    //get the date format
    PyObject *pydateformat=PyObject_GetAttrString(py_config_module,"date_format");
    date_format=PyString_AsString(pydateformat);
    
	//load python classes
	if (load_py_classes(py_base_module) == -1)
		return NULL;
    
    return Py_None;    
}

/*
Procedure exposed in Python to add the tuple: uri, wsgi call back
*/
static PyObject *py_add_wsgi_cb(PyObject *self, PyObject *args)
{
    PyObject *py_tuple;
    if (!PyArg_ParseTuple(args, "O", &py_tuple)) 
        return NULL;
    PyList_Append(py_registered_uri, py_tuple);
    return Py_None;    
}
    
/*
Procedure exposed in Python to expose "parse_query"
*/
PyObject *py_parse_query(PyObject *self, PyObject *args)
{
    char *uri;

    if (!PyArg_ParseTuple(args, "s", &uri))
        return NULL;
    return parse_query(uri);
}

/*
Procedure exposed in Python to register the generic python callback
*/
PyObject *py_set_gen_wsgi_cb(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, "O", &py_generic_cb))
        return NULL;
    return Py_None;
}

PyObject *py_set_debug(PyObject *self, PyObject *args)
{
	unsigned long debug_val = PyInt_AsUnsignedLongMask(args);
	if (debug_val == 1)
		log_level = LOG_DEBUG;
	else
		log_level = LOG_ERR;
	return Py_None;
}

PyObject *py_get_debug(PyObject *self, PyObject *args)
{
	return Py_BuildValue("i", (log_level == LOG_DEBUG) ? 1 : 0);
}

/*
*/
PyObject *py_set_log_level(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, "i", &log_level))
        return NULL;
    return Py_None;
}

/*
*/
PyObject *py_get_log_level(PyObject *self, PyObject *args)
{
    return Py_BuildValue("i", log_level);
}

/*
Procedure exposed in Python to add a timer: delay, python callback method
*/
static PyObject *py_add_timer_cb(PyObject *self, PyObject *args)
{
    struct TimerObj *timer;
    if (list_timers_i<MAX_TIMERS)
    {
        timer = calloc(1,sizeof(struct TimerObj));
        if (!PyArg_ParseTuple(args, "fO", &timer->timeout, &timer->py_cb)) 
            return NULL;
        list_timers[list_timers_i]=timer;
        list_timers_i++;
    } 
    else
    {
        LERROR("Limit of maximum %i timers has been reached", list_timers_i);
    }
    
    return PyInt_FromLong(list_timers_i);    
}

/*
Procedure exposed in Python to stop a running timer: i
*/
static PyObject *py_stop_timer(PyObject *self, PyObject *args)
{
    int i;
    struct TimerObj *timer;
    struct ev_loop *loop = ev_default_loop(0);
    if (!PyArg_ParseTuple(args, "i", &i)) 
        return NULL;
    timer=list_timers[i];
    ev_timer_stop(loop, &timer->timerwatcher);
    
    return Py_None;    
}

/*
Procedure exposed in Python to restart a running timer: i
*/
static PyObject *py_restart_timer(PyObject *self, PyObject *args)
{
    int i;
    struct TimerObj *timer;
    struct ev_loop *loop = ev_default_loop(0);
    if (!PyArg_ParseTuple(args, "i", &i)) 
        return NULL;
    if (i<=list_timers_i)
    {
        timer=list_timers[i];
        ev_timer_again(loop, &timer->timerwatcher);
    }
    else
    {
        LERROR("index out of range: %i", i);
    }
    return Py_None;    
}


/*
Register a python function to execute when idle
*/
PyObject *py_defer(PyObject *self, PyObject *args)
{
    PyObject *pyfct, *pycombined, *pyfctargs;
    int startidle=0;
    int toadd=1;
    int listsize=0;
    
    if (!PyArg_ParseTuple(args, "OOO", &pyfct, &pyfctargs, &pycombined))
        return NULL;
    //if queue is empty, trigger a start of idle
    
    if (!pydeferqueue) 
    {
        pydeferqueue=PyList_New(0);
    }
    listsize=PyList_Size(pydeferqueue);
    if (listsize==0)
    {
        //it has been stopped by the idle_cb
        startidle=1;    
    }
    //add fct cb into the defer queue
    PyObject *pyelem=PyList_New(0);
    PyList_Append(pyelem, pyfct);
    PyList_Append(pyelem, pyfctargs);   
    if (pycombined==Py_True)
    {
        //check if the fucntion is already in the queue
        if (PySequence_Contains(pydeferqueue, pyelem))
        {
            toadd=0;
        }
    }
    
    if (toadd==1)
    {
        PyList_Append(pydeferqueue, pyelem);
        //start the idle
        if (startidle==1)
        {
            //we create a new idle watcher and we start it
            LDEBUG("trigger idle_start");
            ev_idle_start(loop, idle_watcher);
        }
    }
    Py_DECREF(pyelem);
    return Py_None;
}

/*
Return the defer queue size
*/
PyObject *py_defer_queue_size(PyObject *self, PyObject *args)
{
    int listsize;
    if (pydeferqueue)
    {
        listsize=PyList_Size(pydeferqueue);  
        return Py_BuildValue("i", listsize);
    } else
    {
        return Py_None;
    }
}

/*

*/
PyObject *py_rfc1123_date(PyObject *self, PyObject *args)
{
    time_t t;
    PyObject *result;
    char *rfc_string = NULL;
    if (!PyArg_ParseTuple(args, "L", &t))
        return NULL;
    rfc_string = time_rfc1123(t);
    result = PyString_FromString(rfc_string);
    free(rfc_string);
    return result;
}

PyObject *py_write_response(PyObject *self, PyObject *args)
{
	PyObject *py_client, *py_message;
	if (!PyArg_ParseTuple(args, "OO", &py_client, &py_message))
		return NULL;

	struct client *cli = get_client(py_client);
	if (!cli) {
		//Py_INCREF(pyenviron);
		//Py_INCREF(pystart_response);
		//cli = get_current_client();
		//save_client(cli, pyenviron, pystart_response);
		LERROR("py_write_response: unknown client %p", py_client);
		return NULL;
	}
	LDEBUG("py_write_response %p", cli);

	PyObject *o = PyList_GetItem(py_message, 0);
	LDEBUG("py_write_response mesg: %s", PyString_AsString(o));

	//PyObject *pydummy = PyObject_Str(pystart_response);
	PyObject *pystart_response = PyObject_GetAttrString(py_client, "start_response");
	PyObject *pydummy = PyObject_Str(pystart_response);
	Py_DECREF(pystart_response);
	strcpy(cli->response_header, PyString_AsString(pydummy));
	cli->response_header_length = strlen(cli->response_header);
	Py_DECREF(pydummy);
	Py_INCREF(py_message);
	unregister_client(py_client);
	cli->response_content = py_message;
	ev_io_init(&cli->ev_write, write_response_cb, cli->fd,EV_WRITE);
	ev_io_start(loop, &cli->ev_write);
	return Py_None;
}

static int py_set_client_timeout(struct client* cli)
{
LDEBUG(">> ENTER");
	if (!cli || !cli->py_client)
		return -1;
	PyObject* py_client = cli->py_client;

	PyObject *py_timer = PyObject_GetAttrString(py_client, "timeout");
	float timer = 60;
	if (py_timer)
		timer = PyFloat_AsDouble(py_timer);

	int r = set_client_timer(cli, timer >= 0 ? timer : 60, PyObject_GetAttrString(py_client, "timeout_cb"));
	LDEBUG("<< EXIT");
	return r;
}

PyObject *py_register_client(PyObject *self, PyObject *args)
{
	LDEBUG(">> ENTER");
	struct client* cli = get_current_client();
	if (!PyArg_ParseTuple(args, "O", &cli->py_client)) {
		LDEBUG("<< EXIT");
		return Py_False;
	}

	if (py_set_client_timeout(cli) == -1) {
		LDEBUG("<< EXIT");
		return Py_False;
	}

/*	PyObject *py_client;
	if (!PyArg_ParseTuple(args, "O", &py_client))
		return Py_False;
*/
	if (register_client(cli) != 0) {
		LDEBUG("<< EXIT");
		return Py_False;
	}
	LDEBUG("<< EXIT");
	return Py_True;
}

PyObject *py_close_client(PyObject *self, PyObject *args)
{
	PyObject *py_client;
	if (!PyArg_ParseTuple(args, "O", &py_client))
		return Py_False;

	close_client(py_client);
	return Py_True;
}

static PyMethodDef EvhttpMethods[] = {
    {"start", py_ev_start, METH_VARARGS, "Define evhttp sockets"},
    {"set_base_module", py_set_base_module, METH_VARARGS, "set you base module"},
    {"run", py_run_loop, METH_VARARGS, "Run the main loop"},
    {"wsgi_cb", py_add_wsgi_cb, METH_VARARGS, "Add an uri and his wsgi callback in the list of uri to watch"},
    {"wsgi_gen_cb", py_set_gen_wsgi_cb, METH_VARARGS, "Set the generic wsgi callback"},
    {"parse_query", py_parse_query, METH_VARARGS, "parse query into dictionary"},
    {"set_debug", py_set_debug, METH_VARARGS, "Set the debug level (deprecated: use set_log_level instead)"},
    {"get_debug", py_get_debug, METH_VARARGS, "Get the debug level (deprecated: use get_log_level instead)"},
    {"set_log_level", py_set_log_level, METH_VARARGS, "Set log level (syslog levels)"},
    {"get_log_level", py_get_log_level, METH_VARARGS, "Get log level (syslog levels)"},
    {"libev_version", py_libev_version, METH_VARARGS, "Get the libev's ABI version you are using"},
    {"add_timer", py_add_timer_cb, METH_VARARGS, "Add a timer"},
    {"stop_timer", py_stop_timer, METH_VARARGS, "Stop a running timer"},
    {"restart_timer", py_restart_timer, METH_VARARGS, "Restart an existing timer"},
    {"defer", py_defer, METH_VARARGS, "defer the execution of a python function."},
    {"defer_queue_size", py_defer_queue_size, METH_VARARGS, "Get the size of the defer queue"},
    {"rfc1123_date", py_rfc1123_date, METH_VARARGS, "trasnform a time (in sec) into a string compatible with the rfc1123"},
    {"write_response", py_write_response, METH_VARARGS, "Write response to waiting client"},
    {"register_client", py_register_client, METH_VARARGS, "Register client and put it to wait"},
    {"close_client", py_close_client, METH_VARARGS, "Close client"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
init_evwsgi(void)
{
    PyObject *m;
    m = Py_InitModule("_evwsgi", EvhttpMethods);
    
    ServerError = PyErr_NewException("_evwsgi.error", NULL, NULL);
    Py_INCREF(ServerError);
    PyModule_AddObject(m, "error", ServerError);
}
