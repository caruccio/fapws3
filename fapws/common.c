#include "extra.h"
#include "common.h"
#include <Python.h>

static PyClasses _pyclass;

PyObject* load_py_object(PyObject* module, const char* name)
{
	PyObject* o = PyObject_GetAttrString(module, name);
	if (!o) {
		LERROR("Unable to load python class Environ");
		return NULL;
	}
	return o;
}

void unload_py_classes(void)
{
	if (_pyclass.environ)
		Py_DECREF(_pyclass.environ);
	_pyclass.environ = NULL;

	if (_pyclass.start_response)
		Py_DECREF(_pyclass.start_response);
	_pyclass.start_response = NULL;

	if (_pyclass.client)
		Py_DECREF(_pyclass.client);
	_pyclass.client = NULL;
}

int load_py_classes(PyObject* module)
{
	if ((_pyclass.environ = load_py_object(module, "Environ")) == NULL ||
	    (_pyclass.start_response = load_py_object(module, "Start_response")) == NULL ||
	    (_pyclass.client = load_py_object(module, "Client")) == NULL
	   ) {
		unload_py_classes();
		return -1;
	}
	return 0;
}

PyClasses* pyclass()
{
	return &_pyclass;
}

