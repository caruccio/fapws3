#pragma once

#include <ev.h>
#include <Python.h>

#define MAXHEADER 4096

struct TimerObj {
	ev_timer timerwatcher;
	ev_tstamp timeout;
	ev_tstamp repeat;
	PyObject *py_cb;
};

/* available classes from base.py */
typedef struct
{
	PyObject* environ;
	PyObject* start_response;
	PyObject* client;
} PyClasses;

/* management of python classes from base.py */
PyClasses* pyclass(void);
int load_py_classes(PyObject* module);
void unload_py_classes(void);

void start_timer(struct TimerObj *timer, void* cb);
