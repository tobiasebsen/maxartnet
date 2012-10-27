/**
	@file
	maxartnet - artnet external
	tobias ebsen - tobiasebsen@gmail.com

	@ingroup	max externals
*/

#include "ext.h"
#include "ext_obex.h"
#include "ext_common.h"
#include "ext_systhread.h"

#include "artnet.h"

#define STATUS_IDLE		0
#define STATUS_POLLING	1

////////////////////////// object struct
typedef struct _maxartnet
{
	t_object		ob;
	long			sub;
	t_symbol*		shortname;
	t_symbol*		longname;
	long			bcast_limit;
	t_systhread		systhread;
	int				systhread_cancel;
	void*			outlet;
	void*			inputs[4];
	void*			outputs[4];
	artnet_node		node;
	int				status;
	long			polltime;
	int				polltimeout;
	int				nodes_found;
} t_maxartnet;

///////////////////////// function prototypes
void maxartnet_int(t_maxartnet *x, long n);
void maxartnet_poll(t_maxartnet *x);
void maxartnet_getconfig(t_maxartnet *x);
void maxartnet_list(t_maxartnet *x, t_symbol *msg, long argc, t_atom *argv);
void maxartnet_dmx(t_maxartnet *x, t_symbol *msg, long argc, t_atom *argv);
void *maxartnet_new(t_symbol *s, long argc, t_atom *argv);
void maxartnet_free(t_maxartnet *x);
void maxartnet_assist(t_maxartnet *x, void *b, long m, long a, char *s);

t_max_err maxartnet_subnet_get(t_maxartnet *x, void *attr, long *ac, t_atom **av);
t_max_err maxartnet_subnet_set(t_maxartnet *x, void *attr, long ac, t_atom *av);
t_max_err maxartnet_shortname_set(t_maxartnet *x, void *attr, long ac, t_atom *av);
t_max_err maxartnet_longname_set(t_maxartnet *x, void *attr, long ac, t_atom *av);
t_max_err maxartnet_bcastlimit_set(t_maxartnet *x, void *attr, long ac, t_atom *av);


void* maxartnet_threadproc(t_maxartnet* x);

static int maxartnet_reply_handler(artnet_node node, void *pp, void *d);

//////////////////////// global class pointer variable
void *maxartnet_class;


int main(void)
{	
	t_class *c;
	
	c = class_new("artnet", (method)maxartnet_new, (method)maxartnet_free, (long)sizeof(t_maxartnet), 0L /* leave NULL!! */, A_GIMME, 0);
	
	class_addmethod(c, (method)maxartnet_int, "int", A_LONG, 0);
	class_addmethod(c, (method)maxartnet_poll, "poll", A_NOTHING, 0);
	class_addmethod(c, (method)maxartnet_getconfig, "getconfig", A_NOTHING, 0);
	//class_addmethod(c, (method)maxartnet_subnet, "subnet", A_LONG, 0);
	class_addmethod(c, (method)maxartnet_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)maxartnet_dmx, "dmx", A_GIMME, 0);
    class_addmethod(c, (method)maxartnet_assist, "assist", A_CANT, 0);
	
	CLASS_ATTR_LONG(c, "subnet", 0, t_maxartnet, sub);
	CLASS_ATTR_SAVE(c, "subnet", 0);
	CLASS_ATTR_FILTER_CLIP(c, "subnet", 0, 15);
	CLASS_ATTR_ACCESSORS(c, "subnet", (method)maxartnet_subnet_get, (method)maxartnet_subnet_set);
	CLASS_ATTR_LABEL(c, "subnet", 0, "Sub-net");
	
	CLASS_ATTR_SYM(c, "shortname", 0, t_maxartnet, shortname);
	CLASS_ATTR_SAVE(c, "shortname", 0);
	CLASS_ATTR_LABEL(c, "shortname", 0, "Short Name");
	CLASS_ATTR_ACCESSORS(c, "shortname", NULL, (method)maxartnet_shortname_set);
	
	CLASS_ATTR_SYM(c, "longname", 0, t_maxartnet, longname);
	CLASS_ATTR_SAVE(c, "longname", 0);
	CLASS_ATTR_LABEL(c, "longname", 0, "Long Name");
	CLASS_ATTR_ACCESSORS(c, "longname", NULL, (method)maxartnet_longname_set);

	CLASS_ATTR_LONG(c, "bcastlimit", 0, t_maxartnet, bcast_limit);
	CLASS_ATTR_SAVE(c, "bcastlimit", 0);
	CLASS_ATTR_FILTER_CLIP(c, "bcastlimit", 0, 30);
	CLASS_ATTR_LABEL(c, "bcastlimit", 0, "Broadcast Limit");
	CLASS_ATTR_ACCESSORS(c, "bcastlimit", NULL, (method)maxartnet_bcastlimit_set);

	class_register(CLASS_BOX, c);
	maxartnet_class = c;

	post("ARTNET v0.1 by Tobias Ebsen (2012)");

	return 0;
}

void maxartnet_start(t_maxartnet *x)
{
	if (artnet_start(x->node) != ARTNET_EOK) {
		object_error((t_object*)x, artnet_strerror());
		return;
	}	
	x->systhread_cancel = FALSE;
	if (systhread_create((method)maxartnet_threadproc, x, 0, 0, 0, &x->systhread) != MAX_ERR_NONE)
		object_error((t_object*)x, "Error starting systhread");
}

void maxartnet_stop(t_maxartnet *x)
{
	artnet_stop(x->node);
	
	x->systhread_cancel = TRUE;
}

void maxartnet_int(t_maxartnet *x, long n)
{
	long a = proxy_getinlet((t_object*)x);
	
	if (a == 0) {
		if (n)
			maxartnet_start(x);
		else
			maxartnet_stop(x);
	}
	else {
		uint8_t port_id = a - 1;
		artnet_set_port_addr(x->node, port_id, ARTNET_INPUT_PORT, n);
	}
}

void maxartnet_poll(t_maxartnet *x)
{
	t_atom a[2];
	
	atom_setsym(&a[0], gensym("menu"));
	atom_setsym(&a[1], gensym("clear"));
	outlet_atoms(x->outlet, 2, a);

	x->polltime = gettime();
	x->nodes_found = 0;
	x->status = STATUS_POLLING;
	if (artnet_send_poll(x->node, NULL, ARTNET_TTM_DEFAULT) != ARTNET_EOK)
		object_error((t_object*)x, artnet_strerror());
}

void maxartnet_getconfig(t_maxartnet *x)
{
	t_atom a[4];
	
	artnet_node_config_t config;
	artnet_get_config(x->node, &config);
	
	atom_setsym(&a[0], gensym("config"));
	
	// Short name
	atom_setsym(&a[1], gensym("shortname"));
	atom_setsym(&a[2], gensym(config.short_name));
	outlet_atoms(x->outlet, 3, a);
	
	// Long name
	atom_setsym(&a[1], gensym("longname"));
	atom_setsym(&a[2], gensym(config.long_name));
	outlet_atoms(x->outlet, 3, a);

	// Subnet
	atom_setsym(&a[1], gensym("subnet"));
	atom_setlong(&a[2], config.subnet);
	outlet_atoms(x->outlet, 3, a);
	
	/*int i;
	for (i=0; i<ARTNET_MAX_PORTS; i++) {
		atom_setlong(&a, config.in_ports[i]);
		outlet_anything(x->outlet, gensym("config port input"), 1, &a);
	}
	for (i=0; i<ARTNET_MAX_PORTS; i++) {
		atom_setlong(&a, config.out_ports[i]);
		outlet_anything(x->outlet, gensym("config port output"), 1, &a);
	}*/
}

t_max_err maxartnet_subnet_get(t_maxartnet *x, void *attr, long *ac, t_atom **av)
{
	t_max_err r = MAX_ERR_NONE;
	char alloc;
	artnet_node_config_t config;

	if (ac && av) {
		artnet_get_config(x->node, &config);
		x->sub = config.subnet;
		
		r = atom_alloc(ac, av, &alloc);
		if (r != MAX_ERR_NONE)
			return r;

		atom_setlong(*av, x->sub);
	}
	return MAX_ERR_NONE;
}

t_max_err maxartnet_subnet_set(t_maxartnet *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		x->sub = atom_getlong(av);
		artnet_set_subnet_addr(x->node, x->sub);
	}
	return MAX_ERR_NONE;
}

t_max_err maxartnet_shortname_set(t_maxartnet *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		x->shortname = atom_getsym(av);
		artnet_set_short_name(x->node, x->shortname->s_name);
	}
	return MAX_ERR_NONE;
}

t_max_err maxartnet_longname_set(t_maxartnet *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		x->longname = atom_getsym(av);
		artnet_set_long_name(x->node, x->longname->s_name);
	}
	return MAX_ERR_NONE;
}

t_max_err maxartnet_bcastlimit_set(t_maxartnet *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		x->bcast_limit = atom_getlong(av);
		artnet_set_bcast_limit(x->node, x->bcast_limit);
	}
	return MAX_ERR_NONE;
}

void maxartnet_list(t_maxartnet *x, t_symbol *msg, long argc, t_atom *argv)
{
	long a = proxy_getinlet((t_object*)x);
	
	if(a == 0) {
		object_error((t_object*)x, "Lists are not allowed on general input");
		return;
	}

	if (argc > 512) {
		object_error((t_object*)x, "Invalid number of arguments");
		return;
	}
	
	uint8_t port_id = a - 1;
	uint8_t data[argc];
	
	int i;
	for (i=0; i<argc; i++) {
		
		if(argv[i].a_type != A_LONG) {
			object_error((t_object*)x, "Invalid argument type for dmx message");
			return;
		}
		
		int c = atom_getlong(&argv[i]);
		if(c<0 || c>255) {
			object_error((t_object*)x, "Invalid argument value. DMX data must be within byte range.");
			return;
		}
		
		data[i] = c;
	}
	if (artnet_send_dmx(x->node, port_id, argc-1, data) != ARTNET_EOK)
		object_error((t_object*)x, artnet_strerror());
}

void maxartnet_dmx(t_maxartnet *x, t_symbol *msg, long argc, t_atom *argv)
{
	if (argc < 2 || argc > 513) {
		object_error((t_object*)x, "Invalid number of arguments");
		return;
	}
	if(argv[0].a_type != A_LONG) {
		object_error((t_object*)x, "Invalid argument type for dmx message");
		return;
	}
	
	int universe = atom_getlong(&argv[0]);
	if (universe < 0 || universe > 255) {
		object_error((t_object*)x, "Invalid universe");
		return;
	}
	
	uint8_t data[argc-1];
	
	int i;
	for (i=1; i<argc; i++) {

		if(argv[i].a_type != A_LONG) {
			object_error((t_object*)x, "Invalid argument type for dmx message");
			return;
		}
		
		int c = atom_getlong(&argv[i]);
		if(c<0 || c>255) {
			object_error((t_object*)x, "Invalid argument value. DMX data must be within byte range.");
			return;
		}
		
		data[i-1] = c;
	}
	if (artnet_raw_send_dmx(x->node, universe, argc-1, data) != ARTNET_EOK)
		object_error((t_object*)x, artnet_strerror());
}

void maxartnet_assist(t_maxartnet *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) {
		if (a == 0)
			sprintf(s, "int/message\n1 starts, 0 stops, poll requests nodes");
		else
			sprintf(s, "input port %ld\nDMX data list, Int sets address", a);
	} 
	else {
		if (a == 0)
			sprintf(s, "general output");
		else
			sprintf(s, "output port %ld\nDMX data list", a);
	}
}

void maxartnet_free(t_maxartnet *x)
{
	x->systhread_cancel = TRUE;
	artnet_stop(x->node);
	artnet_destroy(x->node);
	
	int i;
	for (i=0; i<4; i++) {
		if (x->inputs[i])
			object_free(x->inputs[i]);
	}
}

void *maxartnet_new(t_symbol *s, long argc, t_atom *argv)
{
	t_maxartnet *x = NULL;
    
	x = (t_maxartnet *)object_alloc(maxartnet_class);
	if (x == NULL)
		return x;
		
	x->outlet = outlet_new(x, NULL);
			
	x->node = artnet_new(NULL, FALSE);
	if (x->node == NULL)
		object_error((t_object*)x, artnet_strerror());
	
	attr_args_process(x, argc, argv);
	argc = attr_args_offset(argc, argv);

	uint8_t type = ARTNET_SRV;
	uint8_t inputs = 0;
	uint8_t outputs = 0;

	if (argc > 0) {

		if (argv[0].a_type == A_SYM) {

			t_symbol* s = atom_getsym(&argv[0]);
			if (strcmp(s->s_name, "server") == 0)
				type = ARTNET_SRV;
			else if (strcmp(s->s_name, "node") == 0)
				type = ARTNET_NODE;
			else if (strcmp(s->s_name, "raw") == 0)
				type = ARTNET_RAW;
			else
				object_error((t_object*)x, "Unsupported node type");
		}
		else if (argv[0].a_type == A_LONG)
			inputs = atom_getlong(&argv[0]);
		
		if (argc > 1) {
			if (argv[0].a_type == A_LONG && argv[1].a_type == A_LONG) {
				outputs= atom_getlong(&argv[1]); 
			}
			if (argv[0].a_type == A_SYM && argv[1].a_type == A_LONG) {
				inputs = atom_getlong(&argv[1]);
			}
			if (argc > 2 && argv[2].a_type == A_LONG) {
				outputs= atom_getlong(&argv[2]);
			}
		}

		//object_error((t_object*)x, "Invalid arguments");
	}
	artnet_set_node_type(x->node, type);

	inputs = CLIP(inputs,0,4);
	outputs = CLIP(outputs,0,4);

	int i;
	for (i=0; i<inputs; i++) {
		artnet_set_port_type(x->node, i, ARTNET_ENABLE_INPUT, ARTNET_PORT_DMX);
		x->inputs[i] = proxy_new(x, i+1, NULL);
	}
	for (i=inputs; i<4; i++)
		x->inputs[i] = NULL;

	for (i=0; i<outputs; i++) {
		artnet_set_port_type(x->node, i, ARTNET_ENABLE_OUTPUT, ARTNET_PORT_DMX);
		x->outputs[i] = outlet_new(x, NULL);
	}
	
	//char str[44];
	//sprintf(str, "Created an ArtNet %s on sub-net %d", type == ARTNET_RAW ? "server" : "node", sub);
	//post(str);
	
	artnet_set_handler(x->node, ARTNET_REPLY_HANDLER, maxartnet_reply_handler, x);
	
	x->status = STATUS_POLLING;
	x->polltime = gettime();
	x->polltimeout = 1000;

	return x;
}

void* maxartnet_threadproc(t_maxartnet* x)
{
	fd_set rd_fds;
	struct timeval tv;
	int max;
	
	int artnet_sd = artnet_get_sd(x->node);

	while (!x->systhread_cancel) {
		
		if (x->status == STATUS_POLLING) {

			FD_ZERO(&rd_fds);
			FD_SET(0, &rd_fds);
			FD_SET(artnet_sd, &rd_fds);
		
			max = artnet_sd;
		
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		
			int n = select(max+1, &rd_fds, NULL, NULL, &tv);
			if (n > 0) {
				if (FD_ISSET(artnet_sd , &rd_fds))
					artnet_read(x->node, 1);
			}
			
			if (gettime() - x->polltime > x->polltimeout)
				x->status = STATUS_IDLE;
		}
		else {
			systhread_sleep(100);
		}

	}
	
	systhread_exit(0);
	
	return NULL;
}

static int maxartnet_reply_handler(artnet_node node, void *pp, void *d) {

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
	
	x->nodes_found++;
	
	t_atom a[4];
	atom_setsym(&a[0], gensym("node"));
	atom_setlong(&a[1], x->nodes_found-1);
	
	atom_setsym(&a[2], gensym("shortname"));
	atom_setsym(&a[3], gensym((char*)ne->shortname));
	outlet_atoms(x->outlet, 4, a);
		
	atom_setsym(&a[2], gensym("longname"));
	atom_setsym(&a[3], gensym((char*)ne->longname));
	outlet_atoms(x->outlet, 4, a);

	atom_setsym(&a[2], gensym("subnet"));
	atom_setlong(&a[3], ne->sub);
	outlet_atoms(x->outlet, 4, a);

	atom_setsym(&a[2], gensym("ports"));
	atom_setlong(&a[3], ne->numbports);
	outlet_atoms(x->outlet, 4, a);

	char s[20];
	sprintf(s, "%d.%d.%d.%d", ne->ip[0], ne->ip[1], ne->ip[2], ne->ip[3]);
	atom_setsym(&a[2], gensym("ip"));
	atom_setsym(&a[3], gensym(s));
	outlet_atoms(x->outlet, 4, a);
	
	sprintf(s, "%02x:%02x:%02x:%02x:%02x:%02x", ne->mac[0], ne->mac[1], ne->mac[2], ne->mac[3], ne->mac[4], ne->mac[5]);
	atom_setsym(&a[2], gensym("mac"));
	atom_setsym(&a[3], gensym(s));
	outlet_atoms(x->outlet, 4, a);
	
	atom_setsym(&a[0], gensym("menu"));
	atom_setsym(&a[1], gensym("append"));
	atom_setsym(&a[2], gensym((char*)ne->shortname));
	outlet_atoms(x->outlet, 3, a);

	return 0;
}