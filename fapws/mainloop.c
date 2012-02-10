// vim: ts=4
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
#include "common.h"
#include "client.h"
#include "extra.h"
#include "wsgi.h"
#include "mainloop.h"

PyObject *pydeferqueue; 
PyObject *py_base_module;  //to store the fapws.base python module
PyObject *py_config_module; //to store the fapws.config module
PyObject *py_registered_uri; //list containing the uri registered and their associated wsgi callback.
#define MAX_BUFF 32768  //read buffer size. bigger faster, but memory foot print bigger
#define MAX_RETRY 9   //number of connection retry

extern int debug;
char * VERSION;
PyObject *py_generic_cb; 
char * date_format;

#define xfree(p) do { if (p) free(p); p = NULL; } while(0)

/*
Just to assure the connection will be nonblocking
*/
int setnonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0)
            return flags;
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) 
            return -1;

    return 0;
}


/*
The procedure call the Environ method called "method" and give pydict has parameter
*/
void update_environ(PyObject *pyenviron, PyObject *pydict, char *method)
{
    PyObject *pyupdate=PyObject_GetAttrString(pyenviron, method);
    PyObject_CallFunction(pyupdate, "(O)", pydict);
    Py_DECREF(pyupdate);
}


static void close_timer_obj(struct TimerObj *tout)
{
	if (tout) {
		Py_XDECREF(tout->py_cb);
	}
}

/*
We just free all the required variables and then close the connection to the client 
*/
void close_connection(struct client *cli)
{
	LDEBUG(">> ENTER cli=%p host=%s:%i", cli, cli->remote_addr, cli->remote_port);
	xfree(cli->input_header);
	xfree(cli->cmd);

	xfree(cli->uri);
	xfree(cli->uri_path);
	xfree(cli->remote_addr);
	Py_XDECREF(cli->py_client);
	Py_XDECREF(cli->response_content);
	Py_XDECREF(cli->response_content_obj);
	close(cli->fd);
	close_timer_obj(&cli->tout);
	xfree(cli);
	LDEBUG("<< EXIT");
}

void timeout_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
	struct TimerObj *tout = ((struct TimerObj*) (((char*)w) - offsetof(struct TimerObj, timerwatcher)));
	struct client *cli = ((struct client*) (((char*)tout) - offsetof(struct client, tout)));

	LDEBUG(">> ENTER cli=%p tout=%p", cli, tout);

	ev_timer_stop(loop, w);
	ev_io_stop(EV_A_ &cli->ev_write);
	ev_io_stop(EV_A_ &cli->ev_read);

	if (tout->py_cb) {
		LDEBUG("calling %p(%p)", tout->py_cb, cli->py_client);
		PyObject *pyarglist = Py_BuildValue("(O)",  cli->py_client);
		PyObject *o = PyEval_CallObject(tout->py_cb, pyarglist);
		Py_DECREF(o);
		Py_DECREF(pyarglist);
	}

	unregister_client(cli);
	close_connection(cli);
	LDEBUG("<< EXIT");
}

/*
 */
void timer_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
	LDEBUG(">> ENTER");
    struct TimerObj *timer= ((struct TimerObj*) (((char*)w) - offsetof(struct TimerObj,timerwatcher)));
    PyObject *resp = PyEval_CallObject(timer->py_cb, NULL);
    if (resp==NULL)
    {
        if (PyErr_Occurred()) 
        { 
             PyErr_Print();
        }
        ev_timer_stop(loop, w);
    }
    if (resp==Py_False)
    {
        ev_timer_stop(loop, w);
    }
    Py_XDECREF(resp);
	LDEBUG("<< EXIT");
}

/*
 */
void idle_cb(struct ev_loop *loop, ev_idle *w, int revents)
{
	LDEBUG(">> ENTER");
    int listsize;
    

    listsize=PyList_Size(pydeferqueue);
    if (listsize>0)
    {
        PyObject *pyelem=PySequence_GetItem(pydeferqueue,0); 
        PyObject *pyfct=PySequence_GetItem(pyelem,0);
        PyObject *pyfctargs=PySequence_GetItem(pyelem,1);
        //execute the python code
		LDEBUG("Execute 1 python function in defer mode:%i", listsize);
        PyObject *response = PyObject_CallFunctionObjArgs(pyfct, pyfctargs, NULL); 
        if (response==NULL) 
        {
			LDEBUG("ERROR!!!! Defer callback function as a problem. \nI remind that it takes always one argumet");
            PyErr_Print();
            //exit(1);
        }
        Py_XDECREF(response);
        Py_DECREF(pyfct);
        Py_DECREF(pyfctargs);
        Py_DECREF(pyelem);
        //remove the element
        PySequence_DelItem(pydeferqueue,0); // don't ask me why, but the delitem has to be after the decrefs
    } else
    {
        //stop idle if queue is empty
		LDEBUG("stop ev_idle");
        ev_idle_stop(loop, w);
        Py_DECREF(pydeferqueue);
        pydeferqueue=NULL;
    }
	LDEBUG("<< EXIT");
}




/*
This procedure loops around the pre-defined registered uri to find if the requested uri match. 
Return 0 is no matches
Return 1 if a match has been found
This procedure update cli->uri_path in case of match. 
*/
int handle_uri(struct client *cli)
{
	int i;
	for (i=0;i<PyList_Size(py_registered_uri);i++)
	{
		PyObject *py_item = PyList_GetItem(py_registered_uri, i);
		if (!py_item)
			continue;

		PyObject *py_uri = PyTuple_GetItem(py_item, 0);
		if (!py_uri)
			continue;

		PyObject *wsgi_cb = PyTuple_GetItem(py_item, 1);
		if (!wsgi_cb)
			continue;

		char *uri = NULL;
		int uri_len = 0;
		if (PyString_AsStringAndSize(py_uri, &uri, &uri_len) != -1) {
			if (strncmp(uri, cli->uri, uri_len) == 0) {
				cli->uri_path = strdup(uri); // will be cleaned with cli
				Py_INCREF(wsgi_cb); //because of GetItem RTFM
				cli->wsgi_cb = wsgi_cb;
				return 1;
			}
		}
	}
	return 0;
}


/*
This is the main python handler that will transform and treat the client html request. 
return 1 if we have found a python object to treat the requested uri
return 0 if not (page not found)
return -1 in case of problem
return -2 in case the request command is not implemented
*/
int python_handler(struct client *cli)
{
	LDEBUG(">> ENTER cli=%p host=%s,port=%i:python_handler:HEADER:\n%s", cli, cli->remote_addr, cli->remote_port, cli->input_header);
    PyObject *pydict, *pydummy;
    int ret;

    //  1)initialise environ
    PyObject *pyenviron=PyObject_CallObject(pyclass()->environ, NULL);
    if (!pyenviron)
    {
         LERROR("Failed to create an instance of Environ");
         abort();
    }
    //  2)transform headers into a dictionary and send it to environ.update_headers
    pydict=header_to_dict(cli);
    if (pydict==Py_None)
    {
        Py_DECREF(pyenviron);
        return -500;
    }
    update_environ(pyenviron, pydict, "update_headers");
    Py_DECREF(pydict);
    //  2bis) we check if the request method is supported
    PyObject *pysupportedhttpcmd = PyObject_GetAttrString(py_base_module, "supported_HTTP_command");
    if (cli->cmd==NULL) pydummy=Py_None; 
    else pydummy = PyString_FromString(cli->cmd);
    if (PySequence_Contains(pysupportedhttpcmd,pydummy)!=1)
    {
        //return not implemented 
        Py_DECREF(pysupportedhttpcmd);
        Py_DECREF(pydummy);
        Py_DECREF(pyenviron);
        return -501;
    }
    Py_DECREF(pydummy);
    //  2ter) we treat directly the OPTIONS command
    if (strcmp(cli->cmd,"OPTIONS")==0)
    {
        pydummy=PyString_FromFormat("HTTP/1.0 200 OK\r\nServer: %s\r\nAllow: ", VERSION) ;
        PyObject *pyitem; 
        int index, max;
        max = PyList_Size(pysupportedhttpcmd);
        for (index=0; index<max; index++)
        {
            pyitem=PyList_GetItem(pysupportedhttpcmd, index);  // no need to decref pyitem
            PyString_Concat(&pydummy, PyObject_Str(pyitem));
            if (index<max-1)
               PyString_Concat(&pydummy, PyString_FromString(", "));
        }
        PyString_Concat(&pydummy, PyString_FromString("\r\nContent-Length: 0\r\n\r\n"));
        strcpy(cli->response_header,PyString_AsString(pydummy));
        cli->response_header_length=strlen(cli->response_header);
        cli->response_content=PyList_New(0);
        Py_DECREF(pyenviron);
        return 1;
    }
    Py_DECREF(pysupportedhttpcmd);
    //  3)find if the uri is registered
    if (handle_uri(cli)!=1)
    {
         if (py_generic_cb==NULL)
         {
            Py_DECREF(pyenviron);
            return 0;
         }
         else
         {
             Py_INCREF(py_generic_cb);
             cli->wsgi_cb=py_generic_cb;
             cli->uri_path=strdup("");
         }
    }
    // 4) build path_info, ...
    pydict=py_build_method_variables(cli);
    update_environ(pyenviron, pydict, "update_uri");
    Py_DECREF(pydict);   
    // 5) in case of POST, put it into the wsgi.input
    if (strcmp(cli->cmd,"POST")==0)
    {
        ret=manage_header_body(cli, pyenviron);
        if (ret < 0) {
            Py_DECREF(pyenviron);
            return ret;
        }
    }
    //  6) add some request info
    pydict=py_get_request_info(cli);
    update_environ(pyenviron, pydict, "update_from_request");
    Py_DECREF(pydict);
    // 7) build response object
    PyObject *pystart_response=PyInstance_New(pyclass()->start_response, NULL, NULL);
    // 7b) add the current date to the response object
    PyObject *py_response_header=PyObject_GetAttrString(pystart_response,"response_headers");
    char *sftime;
    sftime=cur_time_rfc1123();
    pydummy = PyString_FromString(sftime);
    PyDict_SetItemString(py_response_header, "Date", pydummy);
    Py_DECREF(pydummy);
    Py_DECREF(py_response_header);
    xfree(sftime);
    pydummy = PyString_FromString(VERSION);
    PyDict_SetItemString(py_response_header, "Server", pydummy);
    Py_DECREF(pydummy);

	// 8) execute python callbacks with its parameters
	PyObject *pyarglist = Py_BuildValue("(OO)", pyenviron, pystart_response );
	cli->response_content = PyEval_CallObject(cli->wsgi_cb,pyarglist);

	int defer_response = 0;
	if (cli->response_content != NULL) {
		if ((PyFile_Check(cli->response_content)==0) && (PyIter_Check(cli->response_content)==1)) {
			//This is an Iterator object. We have to execute it first
			cli->response_content_obj = cli->response_content;
			cli->response_content = PyIter_Next(cli->response_content_obj);
		} else if (PyBool_Check(cli->response_content) == 1 && Py_True == cli->response_content) {
			defer_response = 1;
			Py_DECREF(cli->response_content);
			cli->response_content = NULL;
		}
	}
	Py_DECREF(pyarglist);
	Py_DECREF(cli->wsgi_cb);

	if (defer_response == 1) {
		//if (register_client(cli) == -1)
		//	return -500;
		return 2;
	} else if (cli->response_content != NULL) {
		PyObject *pydummy = PyObject_Str(pystart_response);
		strcpy(cli->response_header, PyString_AsString(pydummy));
		cli->response_header_length = PyString_Size(pydummy); //strlen(cli->response_header);
		Py_DECREF(pydummy);
	} else  {
		LERROR("Python error!!!");
		if (str_append3(cli->response_header,"HTTP/1.0 500 Not found\r\nContent-Type: text/html\r\nServer: ", VERSION, "\r\n\r\n", MAXHEADER)<0) {
			LDEBUG("ERROR!!!! Response header bigger than foreseen:%i", MAXHEADER);
			LDEBUG("HEADER TOP\n%s\nHEADER BOT", cli->response_header);
			return -1;
		}

		cli->response_header_length=strlen(cli->response_header);
		if (PyErr_Occurred())  {
			//get_traceback();py_b
			PyObject *pyerrormsg_method=PyObject_GetAttrString(py_base_module,"redirectStdErr");
			PyObject *pyerrormsg=PyObject_CallFunction(pyerrormsg_method, NULL);
			Py_DECREF(pyerrormsg_method);
			Py_DECREF(pyerrormsg);
			PyErr_Print();
			PyObject *pysys=PyObject_GetAttrString(py_base_module,"sys");
			PyObject *pystderr=PyObject_GetAttrString(pysys,"stderr");
			Py_DECREF(pysys);
			/*            PyObject *pyclose_method=PyObject_GetAttrString(pystderr, "close");
						 PyObject *pyclose=PyObject_CallFunction(pyclose_method, NULL);
						 Py_DECREF(pyclose_method);
						 Py_DECREF(pyclose);*/
			PyObject *pygetvalue=PyObject_GetAttrString(pystderr, "getvalue");
			Py_DECREF(pystderr);
			PyObject *pyres=PyObject_CallFunction(pygetvalue, NULL);
			Py_DECREF(pygetvalue);
			//test if we must send it to the page
			PyObject *pysendtraceback = PyObject_GetAttrString(py_config_module,"send_traceback_to_browser");
			cli->response_content=PyList_New(0);
			if (pysendtraceback==Py_True) {
				pydummy = PyString_FromString("<h1>Error</h1><pre>");
				PyList_Append(cli->response_content, pydummy );
				Py_DECREF(pydummy);
				PyList_Append(cli->response_content, pyres);
				pydummy = PyString_FromString("</pre>");
				PyList_Append(cli->response_content, pydummy);
				Py_DECREF(pydummy);
			} else {
				PyObject *pyshortmsg = PyObject_GetAttrString(py_config_module,"send_traceback_short");
				PyList_Append(cli->response_content, pyshortmsg);
				Py_DECREF(pyshortmsg);
			}
			Py_DECREF(pyres);
			Py_DECREF(pysendtraceback);
		}
		else
		{
			cli->response_content=PyList_New(0);
			pydummy = PyString_FromString("Page not found.");
			PyList_Append(cli->response_content, pydummy );
			Py_DECREF(pydummy);
		}
	}
	Py_DECREF(pystart_response);
	Py_DECREF(pyenviron);
	LDEBUG("<< EXIT cli=%p", cli);
	return 1;
}

/*
Procedure that will write "len" bytes of "response" to the client.
*/
int write_cli(struct client *cli, char *response, size_t len,  int revents)
{
	LDEBUG(">> ENTER cli=%p host=%s port=%i uri=%s", cli, cli->remote_addr, cli->remote_port, cli->uri);
    /* XXX The design of the function is broken badly: after the first EAGAIN
       error we should exit and wait for an EV_WRITE event. You can think about
       slow client or some client which holds a socket but don't read from one.
     */
    size_t r=0, sent_len=MAX_BUFF;
    if (revents & EV_WRITE){
        while ((int)len > 0)
        {
            if (len<sent_len)
            {
                 sent_len=len;
            }
            r=write(cli->fd,response, sent_len);
			LDEBUG(">> write: len=%i ret=%i", len, r);
            if (((int)r<0) & (errno != EAGAIN))
            {
                if (errno == EPIPE || errno == ECONNRESET) {
                    // The client closes the socket. We can log the error.
                    return 0;
                }
                cli->retry++;
                LERROR("Failed to write to the client:%s:%i, #:%i.", cli->remote_addr, cli->remote_port, cli->retry);
                if (cli->retry>MAX_RETRY) 
                {
                    LERROR("Connection closed after %i retries", cli->retry);
                    return 0; //stop the watcher and close the connection
                }
                // XXX We shouldn't sleep in an event-base server.
                usleep(10000);  //failed but we don't want to close the watcher
            }
            if ((int)r==0)
            {
                return 1;
            }
            if ((int)r>0)
            {
                response+=(int)r;
                len -=r ;
            }
        }
        //p==len
		LDEBUG("<< EXIT ok");
        return 1;
    }
    else {
		LERROR("<< EXIT write callback not ended correctly");
        return 0; //stop the watcher and close the connection
    }

}

/*
This is the write call back registered within the event loop
*/
void write_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{ 
    char response[MAXHEADER];
    int stop=0; //0: not stop, 1: stop, 2: stop and call tp close
    int ret; //python_handler return
    struct client *cli= ((struct client*) (((char*)w) - offsetof(struct client,ev_write)));
	LDEBUG(">> ENTER cli=%p iter=%i", cli, cli->response_iter_sent);
    if (cli->response_iter_sent==-2)
    {
		set_current_client(cli);
		//we must send an header or an error
		ret=python_handler(cli); //look for python callback and execute it
		set_current_client(NULL);
		LDEBUG("python returned: %i", ret);
        if (ret==0)
        {
            //uri not found
            str_append3(response,"HTTP/1.0 500 Not found\r\nContent-Type: text/html\r\nServer: ", VERSION ,"\r\n\r\n<html><head><title>Page not found</head><body><p>Page not found!!!</p></body></html>", MAXHEADER);
            write_cli(cli,response, strlen(response), revents);
            stop=1;
        } 
        else if (ret==-411)
        {
            str_append3(response,"HTTP/1.0 411 Length Required\r\nContent-Type: text/html\r\nServer: ", VERSION, "\r\n\r\n<html><head><title>Length Required</head><body><p>Length Required!!!</p></body></html>", MAXHEADER);
            write_cli(cli,response, strlen(response), revents);
            stop=1;
        }
        else if (ret==-500)
        {
            str_append3(response,"HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/html\r\nServer: ", VERSION, "\r\n\r\n<html><head><title>Internal Server Error</head><body><p>Internal Server Error!!!</p></body></html>", MAXHEADER);
            write_cli(cli,response, strlen(response), revents);
            stop=1;
        }
        else if (ret==-501)
        {
            //problem to parse the request
            str_append3(response,"HTTP/1.0 501 Not Implemented\r\nContent-Type: text/html\r\nServer: ", VERSION, "\r\n\r\n<html><head><title>Not Implemented</head><body><p>Not Implemented!!!</p></body></html>", MAXHEADER);
            write_cli(cli,response, strlen(response), revents);
            stop=1;
        }
		else if (ret==2)
		{
			//dont want to send response right now.
			//save environ and start_response to later send
			stop=0;
			ev_io_stop(EV_A_ w);
		}
        else
        {
            //uri found, we thus send the html header 
            write_cli(cli, cli->response_header, cli->response_header_length, revents);
            cli->response_iter_sent++; //-1: header sent
			LDEBUG("cli=%p iter=%i", cli, cli->response_iter_sent);
        }
    } 
    else if (strcmp(cli->cmd,"HEAD")==0)
    {
        //we don't send additonal data for a HEAD command
        stop=2;
    }
    else 
    {
		LDEBUG("send body %p/%p", cli, cli->response_content);
        //we let the python developer to manage other HTTP command
        if (((PyList_Check(cli->response_content))||(PyTuple_Check(cli->response_content)))  && (cli->response_content_obj==NULL)) //we treat list object
        {
			LDEBUG("Data to send is a tuple");
            int tuple = PyTuple_Check(cli->response_content);
            cli->response_iter_sent++;
			LDEBUG("cli=%p iter=%i", cli, cli->response_iter_sent);
            if (cli->response_iter_sent<(tuple ? PyTuple_Size(cli->response_content) : PyList_Size(cli->response_content))) 
            {
                PyObject *pydummy = tuple ? PyTuple_GetItem(cli->response_content, cli->response_iter_sent) : PyList_GetItem(cli->response_content, cli->response_iter_sent);
                char *buff;
#if (PY_VERSION_HEX < 0x02050000)
                int buflen;
                if (PyObject_AsReadBuffer(pydummy, (const void **) &buff, &buflen)==0)
#else
                Py_ssize_t buflen;
                if (PyObject_AsReadBuffer(pydummy, (const void **) &buff, &buflen)==0)
#endif
                {
                    // if this is a readable buffer, we send it. Other else, we ignore it.
                    if (write_cli(cli, buff, buflen, revents)==0)
                    {
                        cli->response_iter_sent = tuple ? PyTuple_Size(cli->response_content) : PyList_Size(cli->response_content);  //break the for loop
                    }
                }
                else
                {
					LDEBUG("The item %i of your list is not a string!!!! It will be skipped", cli->response_iter_sent);
                }
            }
            else // all iterations has been sent
            {
                stop=2;
            }
        }
        else if (PyFile_Check(cli->response_content) && (cli->response_content_obj==NULL)) // we treat file object
        {
			LDEBUG("Data to send is a file");
			if (cli->response_iter_sent==-1) // we need to initialise the file descriptor
			{
				cli->response_fp=PyFile_AsFile(cli->response_content);
				PyFile_IncUseCount((PyFileObject *)cli->response_content);
			}
			cli->response_iter_sent++;
			LDEBUG("cli=%p iter=%i", cli, cli->response_iter_sent);
            char buff[MAX_BUFF];
            size_t len=fread(buff, MAX_BUFF, sizeof(char), cli->response_fp);
            if (len==0)
            {
                stop=2;
            }
            else
            {
                if (write_cli(cli,buff, len, revents)==0)
                {
                    stop=2;
                }
                if (len<MAX_BUFF)
                {
                    //we have send the whole file
                    stop=2;
					PyFile_DecUseCount((PyFileObject *)cli->response_content);
                }
            }
            //free(buff);
        } 
        else if ((cli->response_content_obj!=NULL) && (PyIter_Check(cli->response_content_obj))) 
        {
			LDEBUG("Data to send is a iterable");
            //we treat Iterator object
            cli->response_iter_sent++;
			LDEBUG("cli=%p iter=%i", cli, cli->response_iter_sent);
            PyObject *pyelem = cli->response_content;
            if (pyelem == NULL) 
            {
                stop = 2;
            }
            else
            {
                char *buff;
#if (PY_VERSION_HEX < 0x02050000)
                int buflen;
                if (PyObject_AsReadBuffer(pyelem, (const void **) &buff, &buflen)==0)
#else
                Py_ssize_t buflen;
                if (PyObject_AsReadBuffer(pyelem, (const void **) &buff, &buflen)==0)
#endif
                {
                    // if this is a readable buffer, we send it. Other else, we ignore it.
                    if (write_cli(cli, buff, buflen, revents)==0)
                    {
                        stop=2;  //break the iterator loop
                    }
                }
                else
                {
					LDEBUG("The item %i of your iterator is not a string!!!! It will be skipped",cli->response_iter_sent);
                }
                Py_DECREF(pyelem);
                cli->response_content = PyIter_Next(cli->response_content_obj);
                if (cli->response_content==NULL)
                {
					LDEBUG("host=%s, port=%i iterator ended uri=%s", cli->remote_addr, cli->remote_port, cli->uri);
                     stop=2;
                }
            }
        }
        else
        {
            PyErr_SetString(PyExc_TypeError, "Result must be a list, a fileobject or an iterable object");
            stop=1;
        }
    }// end of GET OR POST request

	LDEBUG("stop=%i", stop);

    if (stop==2)
    {
      if (cli->response_content!=NULL) {
        if (PyObject_HasAttrString(cli->response_content, "close"))
        {
            PyObject *pydummy=PyObject_GetAttrString(cli->response_content, "close");
            PyObject_CallFunction(pydummy, NULL);
            Py_DECREF(pydummy);
        }
      }
      ev_io_stop(EV_A_ w);
      close_connection(cli);
    }
    if (stop==1)
    {
        ev_io_stop(EV_A_ w);
        close_connection(cli);
    }
	LDEBUG("<< EXIT");
}

void write_response_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct client *cli= ((struct client*) (((char*)w) - offsetof(struct client,ev_write)));

	LDEBUG(">> ENTER cli=%p", cli);
	ev_io_stop(EV_A_ w);

	if (!write_cli(cli, cli->response_header, cli->response_header_length, revents)) {
		close_connection(cli);
	} else {
		cli->response_iter_sent++; //-1: header sent
		LDEBUG("cli=%p iter=%i", cli, cli->response_iter_sent);

		ev_io_init(&cli->ev_write,write_cb,cli->fd,EV_WRITE);
		ev_io_start(loop,&cli->ev_write);
	}
	LDEBUG("<< EXIT");
}
/*
The procedure is the connection callback registered in the event loop
*/
void connection_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{ 
    struct client *cli= ((struct client*) (((char*)w) - offsetof(struct client,ev_read)));
	LDEBUG(">> ENTER cli=%p", cli);
    size_t r=0;
    char rbuff[MAX_BUFF]="";
    int read_finished=0;
    char *err=NULL;
    if (revents & EV_READ){
        r=read(cli->fd,rbuff,MAX_BUFF);
        if ((int)r<0) {
			LERROR("Failed to read the client data. %i tentative", cli->retry);
            cli->retry++;
            if (cli->retry>MAX_RETRY) 
            {
				LERROR("Connection closed after %i retries", cli->retry);
                ev_io_stop(EV_A_ w);
                close_connection(cli);
					LDEBUG("<< EXIT");
                return ;
                }
				LDEBUG("<< EXIT");
            return;
        }
        if ((int)r==0) {
            read_finished=1;
        } 
        else
        {
            cli->input_header=realloc(cli->input_header, (cli->input_pos + r + 1)*sizeof(char));
            memcpy(cli->input_header + cli->input_pos, rbuff, r); 
            cli->input_pos += r; 
            cli->input_header[cli->input_pos]='\0';
			LDEBUG("host=%s,port=%i connection_cb:cli:%p, input_header:%p, input_pos:%i, r:%i", cli->remote_addr, cli->remote_port, cli, cli->input_header, (int)cli->input_pos, (int)r);
            // if \r\n\r\n then end of header   

            cli->input_body=strstr(cli->input_header, "\r\n\r\n"); //use memmem ???
            int header_lentgh =cli->input_body-cli->input_header;
            if (cli->input_body!=NULL)
            {
                //if content-length
                char *contentlenght=strstr(cli->input_header, "Content-Length: ");
                if (contentlenght==NULL)
                {
                    read_finished=1;
                }
                else 
                {
                    int bodylength=strtol(contentlenght+16, &err, 10);
                      //assure we have all body data
                    if ((int)cli->input_pos>=bodylength+4+header_lentgh)
                    {
                        read_finished=1;
                        cli->input_body+=4; // to skip the \r\n\r\n
                    }
                }
            }
         }

		LDEBUG("read has%s finished", read_finished?"":" not");

         if (read_finished)
         {
            ev_io_stop(EV_A_ w);
            if (strlen(cli->input_header)>0)
            {
                ev_io_init(&cli->ev_write,write_cb,cli->fd,EV_WRITE);
                ev_io_start(loop,&cli->ev_write);
            }
            else
            {
                //this is not a normal request, we thus free the parameters created during the initialisation in accept_cb
                close(cli->fd);
                xfree(cli->input_header);
            }
         }
    } else {
		LERROR("read callback not ended correctly");
    }
	LDEBUG("<< EXIT");
}

/*
This is the accept call back registered in the event loop
*/
void accept_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
    int client_fd;
    struct client *cli;
    struct sockaddr_in client_addr;
	LDEBUG(">> ENTER");
    socklen_t client_len = sizeof(client_addr);
    client_fd = accept(w->fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
                return;
        }
    //intialisation of the client struct
    cli = calloc(1,sizeof(struct client));
	LDEBUG("cli=%p host=%s:%i", cli, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    cli->fd=client_fd;
    cli->input_header=malloc(1*sizeof(char));  //will be free'd when we will close the connection
    cli->input_body=NULL;
    cli->uri=NULL;
    cli->cmd=NULL;
    cli->uri_path=NULL;
    cli->wsgi_cb=NULL;
    cli->response_header[0]='\0';
    cli->response_content=NULL;
    cli->response_content_obj=NULL;
    cli->py_client = NULL;
    cli->id = 0;
    cli->input_pos=0;
    cli->retry=0;
    cli->response_iter_sent=-2;
    cli->remote_addr=strdup(inet_ntoa (client_addr.sin_addr));
    cli->remote_port=ntohs(client_addr.sin_port);
	if (setnonblock(cli->fd) < 0) {
		LERROR("setnonblock: %s", strerror(errno));
		close_connection(cli);
		return;
	}
	ev_io_init(&cli->ev_read,connection_cb,cli->fd,EV_READ);
	ev_io_start(loop,&cli->ev_read);
	LDEBUG("<< EXIT");
}

/*
This is the sigint callback registered in the event loop
*/
void sigint_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	ev_unloop(loop, EVUNLOOP_ALL);
	LDEBUG("SIGINT: bye");
}

/*
This is the sigterm callback registered in the event loop
*/
void sigterm_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	ev_unloop(loop, EVUNLOOP_ALL);
	LDEBUG("SIGTERM: bye");
}


/*
This is the sigpipe callback registered in the event loop
*/
void sigpipe_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	//TODO: close socket
	LERROR("SIGPIPE");
}

