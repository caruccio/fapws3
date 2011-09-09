#pragma once

#include <ev.h>
#include <Python.h>

#define MAXHEADER 4096

struct TimerObj {
	ev_timer timerwatcher;
	float delay;
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

