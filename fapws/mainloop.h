#pragma once

#include <ev.h>

void close_connection(struct client *cli);

/* callbacks */
void accept_cb(struct ev_loop *loop, struct ev_io *w, int revents);
void write_response_cb(struct ev_loop *loop, struct ev_io *w, int revents);
void timeout_cb(struct ev_loop *loop, ev_timer *w, int revents);
void timer_cb(struct ev_loop *loop, ev_timer *w, int revents);
void idle_cb(struct ev_loop *loop, ev_idle *w, int revents);
void sigint_cb(struct ev_loop *loop, ev_signal *w, int revents);
void sigterm_cb(struct ev_loop *loop, ev_signal *w, int revents);
void sigpipe_cb(struct ev_loop *loop, ev_signal *w, int revents);
