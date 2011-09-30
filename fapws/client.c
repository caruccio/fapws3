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

	LDEBUG("a=%p   b=%p\n", a, b);
	LDEBUG("ca=%p cb=%p\n", ca?ca->py_client:0, cb?cb->py_client:0);

	if (ca == NULL || ca->py_client < cb->py_client) return -1;
	if (cb == NULL || ca->py_client > cb->py_client) return 1;
	return 0;
}

void terminate_client(void)
{
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
	const struct client* cli;
	printf("[");
	for (cli = (const struct client*)rblookup(RB_LUFIRST, NULL, rb); cli != NULL; cli = (const struct client*)rblookup(RB_LUNEXT, cli, rb))
		printf("%p ", cli);
	printf("]\n");
}

int register_client(struct client *cli)
{
	LDEBUG("register_client %p", cli);
	if (has_client(cli) == 0) {
		if ((cli = (struct client*)rbsearch((void *)cli, rb)) == NULL) {
			return -1;
		}
//		Py_INCREF(cli->py_client);
	}

	LDEBUG("register_client %p (%p)", cli, cli?cli->py_client:NULL);
//	dump();
	return 0;
}

void unregister_client(struct client* cli)
{
	LDEBUG("unregister_client %p", cli);
	if (rblookup(RB_LUEQUAL, cli, rb)) {
		LDEBUG("unregister client %p (%p)", cli, cli->py_client);
		rbdelete(cli, rb);
//		Py_XDECREF(cli->py_client);
	}
}

int has_client(const struct client *cli)
{
	LDEBUG("has_client %p", cli);
	return rblookup(RB_LUEQUAL, cli, rb) == NULL ? 0 : 1;
}

struct client* set_current_client(struct client* cli)
{
	LDEBUG("set_current_client %p", cli);
	struct client* last = _current_client;
	_current_client = cli;
	return last;
}

struct client* get_current_client(void)
{
	LDEBUG("get_current_client %p", _current_client);
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

