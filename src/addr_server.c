/*
 *  addr_server.c -- socket-based ip address server.
 *                   8-92 : Dwayne Fontenot : original coding
 */

#ifdef NeXT
#include <libc.h>
#endif /* NeXT */
#include <sys/ioctl.h>
#include <sys/types.h>
#ifdef __386BSD__
#include <sys/param.h>
#endif /* __386BSD__ */
#if (!defined(NeXT) && !defined(hpux) && !defined(apollo))
#include <unistd.h>
#endif /* NeXT */
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#if !defined(apollo) && !defined(linux)
#include <sys/socketvar.h>
#endif /* !apollo && !linux */
#ifdef _AIX
#include <sys/select.h>
#endif /* _AIX */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <memory.h>
#include "debug.h"
#include "addr_server.h"

#include "config.h"
#include "lint.h"

#ifdef DEBUG_MACRO
int debug_level = 512;
#endif /* DEBUG_MACRO */

/*
 * private local variables.
 */
static connection all_conns[MAX_CONNS];
static int total_conns = 0;
static queue_element_ptr queue_head = NULL;
static queue_element_ptr queue_tail = NULL;
static queue_element_ptr stack_head = NULL;
static int queue_length = 0;
static int conn_fd;

fd_set readmask;

int name_by_ip PROT((int, char *));
int ip_by_name PROT((int, char *));

void init_conns()
{
  int i;

  for(i=0;i<MAX_CONNS;i++){
    all_conns[i].fd = -1;
    all_conns[i].state = CLOSED;
    all_conns[i].sname[0] = '\0';
  }
}

/*
 * Initialize connection socket.
 */
void init_conn_sock(port_number)
     int port_number;
{
  struct sockaddr_in sin;
  int sin_len;
  int optval;

  /*
   * create socket of proper type.
   */
  if((conn_fd = socket(AF_INET,SOCK_STREAM,0)) == -1){
    perror("init_conn_sock: socket");
    exit(1);
  }
  /*
   * enable local address reuse.
   */
  optval = 1;
  if(setsockopt(conn_fd,SOL_SOCKET,SO_REUSEADDR,(char *)&optval,
                sizeof(optval)) == -1){
    perror("init_conn_sock: setsockopt");
    exit(2);
  }
  /*
   * fill in socket address information.
   */
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons((u_short)port_number);
  /*
   * bind name to socket.
   */
  if(bind(conn_fd,(struct sockaddr *)&sin,sizeof(sin)) == -1){
    perror("init_conn_sock: bind");
    exit(3);
  }
  /*
   * get socket name.
   */
  sin_len = sizeof(sin);
  if(getsockname(conn_fd,(struct sockaddr *)&sin,&sin_len) == -1){
    perror("init_conn_sock: getsockname");
    exit(4);
  }
  /*
   * register signal handler for SIGPIPE.
   */
  if(signal(SIGPIPE,sigpipe_handler) == SIGNAL_ERROR){
    perror("init_conn_sock: signal SIGPIPE");
    exit(5);
  }
  /*
   * set socket non-blocking
   */
  if(set_socket_nonblocking(conn_fd, 1) == -1){
    perror("init_user_conn: set_socket_nonblocking 1");
    exit(8);
  }
  /*
   * listen on socket for connections.
   */
  if(listen(conn_fd,SOMAXCONN) == -1){
    perror("init_conn_sock: listen");
    exit(10);
  }
  debug(512,("addr_server: listening for connections on port %d\n",
	     port_number));
}

/*
 * SIGPIPE handler -- does very little for now.
 */
void sigpipe_handler()
{
  fprintf(stderr,"SIGPIPE received.\n");
}

/*
 * I/O handler.
 */
void process_io(nb)
int nb;
{
  int i;

  switch(nb){
  case -1:
    perror("sigio_handler: select");
    break;
  case 0:
    break;
  default:
    /*
     * check for new connection.
     */
    if(FD_ISSET(conn_fd,&readmask)){
      debug(512,("sigio_handler: NEW_CONN\n"));
      enqueue_datapending(conn_fd,NEW_CONN);
    }
    /*
     * check for data pending on established connections.
     */
    for(i=0;i<MAX_CONNS;i++){
      if(FD_ISSET(all_conns[i].fd,&readmask)){
	debug(512,("sigio_handler: CONN\n"));
	enqueue_datapending(all_conns[i].fd,CONN);
      }
    }
    break;
  }
}

INLINE void process_queue()
{
  int i;

  for(i=0;queue_head && (i < MAX_EVENTS_TO_PROCESS);i++){
    handle_top_event();
    dequeue_top_event();
  }
}

void enqueue_datapending(fd, fd_type)
     int fd;
     int fd_type;
{
  queue_element_ptr new_queue_element;

  pop_queue_element(&new_queue_element);
  new_queue_element->event_type = fd_type;
  new_queue_element->fd = fd;
  new_queue_element->next = NULL;
  if(queue_head){
    queue_tail->next = new_queue_element;
  }
  else {
    queue_head = new_queue_element;
  }
  queue_tail = new_queue_element;
}

void dequeue_top_event()
{
  queue_element_ptr top_queue_element;
  if(queue_head){
    top_queue_element = queue_head;
    queue_head = queue_head->next;
    push_queue_element(top_queue_element);
  }
  else {
    fprintf(stderr,"dequeue_top_event: tried to dequeue from empty queue!\n");
  }
}

void pop_queue_element(the_queue_element)
     queue_element_ptr *the_queue_element;
{
  if((*the_queue_element = stack_head)) stack_head = stack_head->next;
  else *the_queue_element = (queue_element_ptr)malloc(sizeof(queue_element));
  queue_length++;
}

void push_queue_element(the_queue_element)
     queue_element_ptr the_queue_element;
{
  the_queue_element->next = stack_head;
  stack_head = the_queue_element;
  queue_length--;
}

void handle_top_event()
{
  switch(queue_head->event_type){
  case NEW_CONN:
    debug(512,("handle_top_event: NEW_CONN\n"));
    new_conn_handler();
    break;
  case CONN:
    debug(512,("handle_top_event: CONN data on fd %d\n",queue_head->fd));
    conn_data_handler(queue_head->fd);
    break;
  default:
    fprintf(stderr,"handle_top_event: unknown event type %d\n",
            queue_head->event_type);
    break;
  }
}

/*
 * This is the new connection handler. This function is called by the
 * event handler when data is pending on the listening socket (conn_fd).
 * If space is available, an interactive data structure is initialized and
 * the connected is established.
 */
void new_conn_handler()
{
  struct sockaddr_in client;
  int client_len;
  struct hostent *c_hostent;
  int new_fd;
  int conn_index;

  client_len = sizeof(client);
  new_fd = accept(conn_fd,(struct sockaddr *)&client,(int *)&client_len);
  if(new_fd == -1){
    perror("new_conn_handler: accept");
    return;
  }
  if(total_conns >= MAX_CONNS){
    char *message = "no available slots -- closing connection.\n";

    fprintf(stderr,"new_conn_handler: no available connection slots.\n");
    write(new_fd,message,strlen(message));
    if(close(new_fd) == -1)
      perror("new_conn_handler: close");
    return;
  }
  /* get some information about new connection */
  c_hostent = gethostbyaddr((char *)&client.sin_addr.s_addr,
			    sizeof(client.sin_addr.s_addr),AF_INET);
  for(conn_index = 0;conn_index < MAX_CONNS;conn_index++){
    if(all_conns[conn_index].state == CLOSED){
      debug(512,("new_conn_handler: opening conn index %d\n",conn_index));
      /* update global data for new fd */
      all_conns[conn_index].fd = new_fd;
      all_conns[conn_index].state = OPEN;
      all_conns[conn_index].addr = client;
      strcpy(all_conns[conn_index].sname,c_hostent->h_name);
      total_conns++;
      return;
    }
  }
  fprintf(stderr,"new_conn_handler: sanity check failed!\n");
}

#define IN_BUF_SIZE 80

void conn_data_handler(fd)
     int fd;
{
  int conn_index;
  int buf_index;
  int num_bytes;
  int msgtype;
  static char buf[IN_BUF_SIZE];

  if((conn_index = index_by_fd(fd)) == -1){
    fprintf(stderr,"conn_data_handler: invalid fd.\n");
    return;
  }
  debug(512,("conn_data_handler: read on fd %d\n",fd));
  num_bytes = read(fd,buf,IN_BUF_SIZE);
  switch(num_bytes){
  case -1:
    switch(errno){
    case EWOULDBLOCK:
      debug(512,("conn_data_handler: read on fd %d: Operation would block.\n",
                 fd));
      break;
    default:
      perror("conn_data_handler: read");
      terminate(conn_index);
      break;
    }
    break;
  case 0:
    if(all_conns[conn_index].state == CLOSED)
      fprintf(stderr,"get_user_data: tried to read from closed fd.\n");
    terminate(conn_index);
    break;
  default:
    debug(512,("conn_data_handler: read %d bytes on fd %d\n",num_bytes,fd));
    buf_index = 0;
    while(buf_index < num_bytes){
      memcpy((char *)&msgtype,(char *)&buf[buf_index],sizeof(int));
      debug(512,("conn_data_handler: message type: %d\n",msgtype));
      switch(msgtype){
      case NAMEBYIP:
	buf_index += name_by_ip(conn_index,&buf[buf_index]);
	break;
/* The reverse to the above... */
      case IPBYNAME:
        buf_index += ip_by_name(conn_index,&buf[buf_index]);
        break;
      default:
	fprintf(stderr,"conn_data_handler: unknown message type %d\n",msgtype);
	buf_index++;
	break;
      }
    }
    break;
  }
}

#define OUT_BUF_SIZE 80

int ip_by_name(conn_index, buf)
     int conn_index;
     char *buf;
{
  struct hostent *hp;
  struct in_addr my_in_addr;
  static char out_buf[OUT_BUF_SIZE];

  hp = gethostbyname(&buf[sizeof(int)]);
  if (hp == NULL) {
/* Failed :( */
    sprintf(out_buf, "%s %s\n",&buf[sizeof(int)],"0");
    debug(512,("%s",out_buf));
    write(all_conns[conn_index].fd, out_buf, strlen(out_buf));
  } else {
/* Success! */
    memcpy(&my_in_addr, hp->h_addr, sizeof(struct in_addr));
    sprintf(out_buf, "%s %s\n", &buf[sizeof(int)],
                                inet_ntoa(my_in_addr));
    debug(512,("%s",out_buf));
    write(all_conns[conn_index].fd, out_buf, strlen(out_buf));
  }
  return(sizeof(int) + strlen(&buf[sizeof(int)]) + 1);
} /* ip_by_name() */

int name_by_ip(conn_index, buf)
     int conn_index;
     char *buf;
{
  long addr;
  struct hostent *hp;
  static char out_buf[OUT_BUF_SIZE];

  if((addr = inet_addr(&buf[sizeof(int)])) == -1){
    debug(512,("name_by_ip: malformed address request.\n"));
    return(sizeof(int) + strlen(&buf[sizeof(int)]) + 1);
  }
  if((hp = gethostbyaddr((char *)&addr,sizeof(addr),AF_INET))){
    sprintf(out_buf,"%s %s\n",&buf[sizeof(int)],hp->h_name);
    debug(512,("%s",out_buf));
    write(all_conns[conn_index].fd,out_buf,strlen(out_buf));
  } else {
    sprintf(out_buf,"%s 0\n",&buf[sizeof(int)]);
    debug(512,("%s",out_buf));
    write(all_conns[conn_index].fd,out_buf,strlen(out_buf));
    debug(512,("name_by_ip: unable to resolve address.\n"));
  }
  return(sizeof(int) + strlen(&buf[sizeof(int)]) + 1);
}

int index_by_fd(fd)
     int fd;
{
  int i;

  for(i=0;i<MAX_CONNS;i++){
    if((all_conns[i].state == OPEN) && (all_conns[i].fd == fd)) return(i);
  }
  return(-1);
}

void terminate(conn_index)
     int conn_index;
{
  if(conn_index < 0 || conn_index >= MAX_CONNS){
    fprintf(stderr,"terminate: conn_index %d out of range.\n",conn_index);
    return;
  }
  if(all_conns[conn_index].state == CLOSED){
    fprintf(stderr,"terminate: connection %d already closed.\n",conn_index);
    return;
  }
  debug(512,("terminating connection %d\n",conn_index));

  if(close(all_conns[conn_index].fd) == -1){
    perror("terminate: close");
    return;
  }
  all_conns[conn_index].state = CLOSED;
  total_conns--;
}

int main(argc,argv)
     int argc;
     char *argv[];
{
  int addr_server_port;
  struct timeval timeout;
  int i;
  int nb;

  if(argc > 1){
    if((addr_server_port = atoi(argv[1])) == 0){
      fprintf(stderr,"addr_server: malformed port number.\n");
      exit(2);
    }
  }
  else {
    fprintf(stderr,"addr_server: first arg must be port number.\n");
    exit(1);
  }
  init_conn_sock(addr_server_port);
  while(1){
    /*
     * use finite timeout for robustness.
     */
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    /*
     * clear selectmasks.
     */
    FD_ZERO(&readmask);
    /*
     * set new connection accept fd in readmask.
     */
    FD_SET(conn_fd,&readmask);
    /*
     * set active fds in readmask.
     */
    for(i=0;i<MAX_CONNS;i++){
      if(all_conns[i].state == OPEN)
	FD_SET(all_conns[i].fd,&readmask);
    }
    nb = select(FD_SETSIZE, &readmask, (fd_set *)0, (fd_set *)0, &timeout);
    if(nb != 0)
      process_io(nb);
    process_queue();
  }
}
