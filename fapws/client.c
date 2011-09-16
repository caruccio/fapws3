#include "extra.h"
#include "client.h"
#include "mainloop.h"
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

static struct TimerObj *create_timer(void)
{
	struct TimerObj *t = malloc(sizeof(*t));
	if (!t)
		return NULL;
	memset(&t->timerwatcher, 0, sizeof(t->timerwatcher));
	t->timeout = 0;
	t->repeat = 0;
	t->py_cb = NULL;
	return t;
}

int set_client_timer(struct client* cli, float timeout, PyObject* py_cb)
{
LDEBUG(">> ENTER");
/*	if (!cli->tout) {
		if ((cli->tout = create_timer()) == NULL) {
			LDEBUG("<< EXIT");
			return -1;
		}
	}
	cli->tout->timeout = timeout;
	cli->tout->repeat = 0;
	*/
	memset(&cli->tout.timerwatcher, 0, sizeof(cli->tout.timerwatcher));
	cli->tout.timeout = timeout;
	cli->tout.repeat = 0;
	cli->tout.py_cb = py_cb;
	start_timer(&cli->tout, &timeout_cb);
LDEBUG("<< EXIT");
	return 0;
}

static int _saved = 0;
int register_client(struct client *cli)
{
	if (cli->py_client)
		Py_INCREF(cli->py_client);

	_clients[_saved++] = cli;
	LDEBUG("saved[%i]=%p->%p", _saved - 1, cli, cli->py_client);
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

void close_client(PyObject* py_client)
{
	struct client* cli = get_client(py_client);
	if (cli)
		close_connection(cli);
}
