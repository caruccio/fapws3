#include "extra.h"
#include "client.h"
#include "mainloop.h"
#include <Python.h>
#include <redblack.h>
#include <stddef.h>

#define CLIENT_BY_ID(_item) ((struct client *) (((char*)_item) - offsetof(struct client, id)))

static struct client* _current_client = NULL;

long get_py_client_id(PyObject *py_client)
{
	PyObject *id = PyObject_CallMethod(py_client, "id", NULL);
	if (PyLong_Check(id)) {
		Py_XDECREF(id);
		return -1;
	}
	const long lid = PyLong_AsLong(id);
	Py_DECREF(id);
	return lid;
}

/*
	TODO: Clearly we need a fast rbtree to store clients.
	Also it would be much better to store in a known struct and expose it to python as a class.
*/
//static struct client* _clients[10000000];
static struct rbtree *rb;

static int compare_client(const void *a, const void *b, const void *c)
{
	register const long id_a = *(const long *)a;
	register const long id_b = *(const long *)b;

	return (id_a < id_b) ? -1 :
	       (id_a > id_b) ?  1 : 0;
/*
	const PyObject *pycli = (const PyObject *)a;
	const struct client* cli = (const struct client*)b;

	LDEBUG("   a=%p   b=%p/%p\n", pycli, cli, cli->py_client);
//	LDEBUG("a=%p/%p b=%p/%p\n", ca, ca?ca->py_client:0, cb, cb?cb->py_client:0);

	if (pycli < cli->py_client) return -1;
	if (pycli > cli->py_client) return 1;
	return 0;
*/
}

void terminate_client(void)
{
	LDEBUG("destroy rbtree");
	if (rb) {
//		struct client* cli;
//		for (cli = (struct client*)rblookup(RB_LUFIRST, NULL, rb); cli != NULL; cli = (struct client*)rblookup(RB_LUNEXT, cli, rb)) {
//			Py_XDECREF(cli->py_client);
//		}
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

static void dump(void)
{
	printf("dump clients: [ ");
	const long *id = rblookup(RB_LUFIRST, NULL, rb);
	while (id) {
		const struct client *cli = CLIENT_BY_ID(id);
		printf("\'%p (%li)\' ", cli, cli->id);
		id = rblookup(RB_LUNEXT, id, rb);
	}
	printf("]\n");
}

int register_client(struct client *cli)
{
	cli->id = get_py_client_id(cli->py_client);
	LDEBUG("register_client += %p/%p (id=%li)", cli, cli->py_client, cli?cli->id:0);
	if (CLIENT_BY_ID(rbsearch(&cli->id, rb)) != cli) {
		return -1;
	}
//		Py_INCREF(cli->py_client);
//	}

	LDEBUG("registered client %p (%p)", cli, cli?cli->py_client:NULL);
	if (log_level >= LOG_DEBUG)
		dump();
	return 0;
}

void unregister_client(struct client* cli)
{
	LDEBUG("unregister_client %p", cli);
/*	if (rbfind(get_py_client_id(&cli->id), rb)) {
		LDEBUG("unregister client %p (%p)", cli, cli->py_client);
		rbdelete(cli, rb);
//		Py_XDECREF(cli->py_client);
	}
*/
	rbdelete(&cli->id, rb);
	if (log_level >= LOG_DEBUG)
		dump();
}

struct client *get_client(PyObject *py_client)
{
	long id = get_py_client_id(py_client);
	LDEBUG("get_client %p (%li)", py_client, id);
	if (id == -1)
		return NULL;
	return CLIENT_BY_ID(rbfind(&id, rb));
}

struct client* set_current_client(struct client* cli)
{
	LDEBUG("set_current_client %p (id=%li)", cli, cli?cli->id:0);
	struct client* last = _current_client;
	_current_client = cli;
	return last;
}

struct client* get_current_client(void)
{
	LDEBUG("get_current_client %p (id=%li)", _current_client, _current_client?_current_client->id:0);
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

