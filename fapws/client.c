#include "extra.h"
#include "client.h"
#include <Python.h>

static struct client* _current_client = NULL;

/*
	TODO: Clearly we need a fast rbtree to store clients.
	Also it would be much better to store in a known struct and expose it to python as a class.
*/
static struct client* _clients[100000];

struct client* set_current_client(struct client* cli)
{
	struct client* last = _current_client;
	 _current_client = cli;
	return last;
}

struct client* get_current_client(void)
{
	return _current_client;
}

static int _saved = 0;
int register_client(PyObject *py_client, struct client *cli)
{
	Py_INCREF(py_client);
	cli->py_client = py_client;

	_clients[_saved++] = cli;
	LDEBUG("saved %i %p->%p", _saved - 1, cli, py_client);
	return 0;
}

void unregister_client(PyObject *py_client)
{
	//struct client* cli = get_client(py_client);
}

struct client* get_client(PyObject *py_client)
{
	int i;
	for (i = 0; i < _saved; ++i) {
		if (_clients[i]->py_client == py_client) {
			return _clients[i];
		}
	}
	return NULL;
}

