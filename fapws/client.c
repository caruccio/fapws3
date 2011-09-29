#include "extra.h"
#include "client.h"
#include "mainloop.h"
#include <Python.h>
#include <redblack.h>

static struct client* _current_client = NULL;

/*
	TODO: Clearly we need a fast rbtree to store clients.
	Also it would be much better to store in a known struct and expose it to python as a class.
*/
//static struct client* _clients[10000000];
static struct rbtree *rb;

static int compare_client(const void* a, const void* b, const void* c)
{
	const struct client* ca = (const struct client*)a;
	const struct client* cb = (const struct client*)b;

	if (ca && cb) {
		return (ca->py_client < cb->py_client) ? -1 :
	       (ca->py_client > cb->py_client) ?  1 :
	       0;
	}
	return -1;
}

void terminate_client(void)
{
	if (rb) {
		struct client* cli;
		for (cli = (struct client*)rblookup(RB_LUFIRST, NULL, rb); cli != NULL; cli = (struct client*)rblookup(RB_LUNEXT, cli, rb)) {
			Py_XDECREF(cli->py_client);
		}
		rbdestroy(rb);
	}
	rb = NULL;
}

int init_client(void)
{
	if (rb)
		terminate_client();

	if ((rb=rbinit(compare_client, NULL)) == NULL) {
		return -1;
	}

	return 0;
}

//static int _saved = 0;
int register_client(struct client *cli)
{
	struct client* c = get_client(cli->py_client);

	if (!c) {
		if ((c = (struct client*)rbsearch((void *)cli, rb)) == NULL) {
			return -1;
		}
	}

	if (cli->py_client)
		Py_INCREF(cli->py_client);

	LDEBUG("register client %p (%p)", cli, cli?cli->py_client:NULL);
	return 0;
}

void unregister_client(PyObject *py_client)
{
	struct client* cli = (struct client*)rblookup(RB_LUEQUAL, py_client, rb);

	if (cli) {
		LDEBUG("unregister client %p (%p)", cli, py_client);
		rbdelete(cli, rb);
		Py_XDECREF(cli->py_client);
	}

	//struct client* cli = get_client(py_client);
}

struct client* get_client(PyObject *py_client)
{
	return rblookup(RB_LUEQUAL, NULL, rb);
}

void close_client(PyObject* py_client)
{
	struct client* cli = get_client(py_client);
	if (cli)
		close_connection(cli);
}

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

//
// Timer
//
/*static struct TimerObj *create_timer(void)
{
	struct TimerObj *t = malloc(sizeof(*t));
	if (!t)
		return NULL;
	memset(&t->timerwatcher, 0, sizeof(t->timerwatcher));
	t->timeout = 0;
	t->repeat = 0;
	t->py_cb = NULL;
	return t;
}*/

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

