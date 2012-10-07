/**
	@file
	artnet - artnet external
	tobias ebsen - tobiasebsen@gmail.com

	@ingroup	max externals
*/

#include "ext.h"
#include "ext_obex.h"
#include "ext_systhread.h"

#include "artnet.h"

////////////////////////// object struct
typedef struct _maxartnet
{
	t_object		ob;
	t_systhread		systhread;
	int				systhread_cancel;
	void*			outlet;
	artnet_node		node;
	int				nodes_found;
} t_maxartnet;

///////////////////////// function prototypes
void maxartnet_int(t_maxartnet *x, long n);
void maxartnet_poll(t_maxartnet *x);
void maxartnet_config(t_maxartnet *x);
void maxartnet_shortname(t_maxartnet *x, t_symbol *s);
void maxartnet_longname(t_maxartnet *x, t_symbol *s);
void maxartnet_subnet(t_maxartnet *x, long n);
void *maxartnet_new(t_symbol *s, long argc, t_atom *argv);
void maxartnet_free(t_maxartnet *x);
void maxartnet_assist(t_maxartnet *x, void *b, long m, long a, char *s);

void* maxartnet_threadproc(t_maxartnet* x);

static int maxartnet_reply_handler(artnet_node node, void *pp, void *d);

//////////////////////// global class pointer variable
void *maxartnet_class;


int main(void)
{	
	t_class *c;
	
	c = class_new("artnet", (method)maxartnet_new, (method)maxartnet_free, (long)sizeof(t_maxartnet), 0L /* leave NULL!! */, A_GIMME, 0);
	
	class_addmethod(c, (method)maxartnet_int, "int", A_LONG, 0);
	class_addmethod(c, (method)maxartnet_poll, "poll", A_NOTHING);
	class_addmethod(c, (method)maxartnet_config, "config", A_NOTHING);
	class_addmethod(c, (method)maxartnet_shortname, "shortname", A_SYM);
	class_addmethod(c, (method)maxartnet_longname, "longname", A_SYM);
	class_addmethod(c, (method)maxartnet_subnet, "subnet", A_LONG);
    class_addmethod(c, (method)maxartnet_assist, "assist", A_CANT, 0);
	
	class_register(CLASS_BOX, c);
	maxartnet_class = c;

	post("ARTNET v0.1 Initialized");

	return 0;
}

void maxartnet_int(t_maxartnet *x, long n)
{
	if (n) {
		artnet_start(x->node);
		
		x->systhread_cancel = FALSE;
		systhread_create((method)maxartnet_threadproc, x, 0, 0, 0, &x->systhread);
	}
	else {
		artnet_stop(x->node);
		
		x->systhread_cancel = TRUE;
	}

}

void maxartnet_poll(t_maxartnet *x)
{
	x->nodes_found = 0;
	artnet_send_poll(x->node, NULL, ARTNET_TTM_DEFAULT);
}

void maxartnet_config(t_maxartnet *x)
{
	t_atom a;
	
	artnet_node_config_t config;
	artnet_get_config(x->node, &config);
	
	atom_setsym(&a, gensym(config.short_name));
	outlet_anything(x->outlet, gensym("config shortname"), 1, &a);
	
	atom_setsym(&a, gensym(config.long_name));
	outlet_anything(x->outlet, gensym("config longname"), 1, &a);

	atom_setlong(&a, config.subnet);
	outlet_anything(x->outlet, gensym("config subnet"), 1, &a);
	
	int i;
	for (i=0; i<ARTNET_MAX_PORTS; i++) {
		atom_setlong(&a, config.in_ports[i]);
		outlet_anything(x->outlet, gensym("config port input"), 1, &a);
	}
	for (i=0; i<ARTNET_MAX_PORTS; i++) {
		atom_setlong(&a, config.out_ports[i]);
		outlet_anything(x->outlet, gensym("config port output"), 1, &a);
	}
}

void maxartnet_shortname(t_maxartnet *x, t_symbol *s)
{
	artnet_set_short_name(x->node, s->s_name);
}

void maxartnet_longname(t_maxartnet *x, t_symbol *s)
{
	artnet_set_long_name(x->node, s->s_name);
}

void maxartnet_subnet(t_maxartnet *x, long n)
{
	artnet_set_subnet_addr(x->node, (uint8_t)n);
}

void maxartnet_assist(t_maxartnet *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) {
		//sprintf(s, "I am inlet %ld", a);
	} 
	else {
		//sprintf(s, "I am outlet %ld", a); 			
	}
}

void maxartnet_free(t_maxartnet *x)
{
	artnet_destroy(x->node);
}

void *maxartnet_new(t_symbol *s, long argc, t_atom *argv)
{
	t_maxartnet *x = NULL;
    
	if (x = (t_maxartnet *)object_alloc(maxartnet_class)) {
		
		x->outlet = outlet_new(x, NULL);
		
		x->node = artnet_new(NULL, FALSE);
		if (x->node == NULL)
			object_error((t_object*)x, artnet_strerror());
		
		artnet_set_handler(x->node, ARTNET_REPLY_HANDLER, maxartnet_reply_handler, x);		
	}
	return (x);
}

void* maxartnet_threadproc(t_maxartnet* x)
{
	fd_set rd_fds;
	struct timeval tv;
	int max;
	
	int artnet_sd = artnet_get_sd(x->node);

	while (!x->systhread_cancel) {
		
		FD_ZERO(&rd_fds);
		FD_SET(0, &rd_fds);
		FD_SET(artnet_sd, &rd_fds);
		
		max = artnet_sd;
		
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		int n = select(max+1, &rd_fds, NULL, NULL, &tv);
		if (n > 0) {
			if (FD_ISSET(artnet_sd , &rd_fds))
	    		artnet_read(x->node, 0);
		}
	}
	
	systhread_exit(0);
	
	return NULL;
}

static int maxartnet_reply_handler(artnet_node node, void *pp, void *d) {

	t_atom a[2];
	t_maxartnet* x = NULL;
	artnet_node_list nl;
	artnet_node_entry ne = NULL;
	
	x = (t_maxartnet*)d;
	nl = artnet_get_nl(x->node);
	
	if (artnet_nl_get_length(nl) == x->nodes_found)
		return 0;

	if(x->nodes_found == 0)
		ne = artnet_nl_first(nl);
	else
		ne = artnet_nl_next(nl);
	
	char s[16];
	sprintf(s, "%d.%d.%d.%d", ne->ip[0], ne->ip[1], ne->ip[2], ne->ip[3]);
	atom_setsym(&a[0], gensym(s));
	atom_setsym(&a[1], gensym((char*)ne->shortname));
	outlet_anything(x->outlet, gensym("node"), 2, a);
	
	return 0;
}