/***************************************************************************
                           conn.c  -  description
                             -------------------
    begin                : Wed Mar 3 2004
    copyright            : (C) 2004 by Stockholm university, Sweden
    email                : roland@catalogix.se

   COPYING RESTRICTIONS APPLY.  See COPYRIGHT File in top level directory
   of this package for details.

 **************************************************************************/

#include "locl.h"
RCSID("$Id$");

int conn_readn( conn_t *ct, char *str, size_t max ) ;
int conn_writen( conn_t *ct, char *str, size_t n ) ;
int conn_close( conn_t *conn )  ;

void conn_iobuf_clear( conn_t *con ) ;

/* ---------------------------------------------------------------------- */

void conn_init( conn_t *conn )
{
  /* traceLog("Init Connection") ; */

  conn->in = iobuf_new( SPOCP_IOBUFSIZE ) ;
  conn->out = iobuf_new( SPOCP_IOBUFSIZE ) ;

  conn->readn = conn_readn;
  conn->writen = conn_writen;
  conn->close = conn_close;

  conn->tls = 0 ;
}

conn_t *conn_new( void )
{
  conn_t *con = 0 ;

  con = ( conn_t * ) Calloc (1, sizeof( conn_t )) ;

  conn_init( con ) ;

  return con ;
}

/* ---------------------------------------------------------------------- */

int conn_close( conn_t *conn ) 
{
  traceLog( "connection on %d closed", conn->fd ) ;
  close( conn->fd ) ;
  conn->fd = 0 ;

  return TRUE ;
}

static void conn_env_reset( conn_t *con ) 
{
  if( con ) {
    if( con->subjectDN ) {
      free( con->subjectDN ) ;
      con->subjectDN = 0 ;
    }
    if( con->issuerDN ) {
      free( con->issuerDN ) ;
      con->issuerDN = 0 ;
    }
    if( con->cipher ) {
      free( con->cipher ) ;
      con->cipher = 0 ;
    }
    if( con->ssl_vers ){
      free( con->ssl_vers ) ;
      con->ssl_vers = 0 ;
    }
    if( con->transpsec ){
      free( con->transpsec ) ;
      con->transpsec = 0 ;
    }
  }
}

void conn_reset( conn_t *conn ) 
{
  if( conn ) {
    if( conn->fd ) conn_close( conn ) ;

    conn->status = CNST_FREE ;
    conn->fd = 0 ;
    if( conn->sri.hostname ) free( conn->sri.hostname ) ;
    if( conn->sri.hostaddr ) free( conn->sri.hostaddr ) ;
    conn_env_reset( conn ) ;
    
    conn_iobuf_clear( conn ) ;
  }
}

void conn_free( conn_t *con )
{
  if( con ) {
    conn_reset( con ) ;
    free( con->in ) ;
    free( con->out ) ;
    free( con ) ;
  }
}

/* ---------------------------------------------------------------------- */

int conn_setup( conn_t *conn, srv_t *srv, int fd, char *host )
{
  conn->srv = srv ;
  conn->fd = fd ;
  conn->status = CNST_LINGERING ;
  conn->con_type = NATIVE ;
  conn->sri.hostaddr = Strdup(host);

  conn->rs = srv->root ;
  conn->tls = 0 ;
  conn->stop = 0 ;

  time( &conn->last_event ) ;

  return 0 ;
}

/* ---------------------------------------------------------------------- */

void conn_iobuf_clear( conn_t *con )
{
  iobuf_flush( con->in ) ;
  iobuf_flush( con->out ) ;
}

/* ---------------------------------------------------------------------- */

char *next_line( conn_t *conn )
{
  char          *s, *b ;
  spocp_iobuf_t *io = conn->in ;
  
  if(( s = strpbrk( io->r, "\r\n" )) == 0 ) { /* try to read some more */
    spocp_conn_read( conn ) ; 
    if(( s = strpbrk( io->r, "\r\n" )) == 0 ) {  /* fail !! */
      return 0 ;
    }
  } 

  if( *s == '\r' && *(s+1) == '\n' ) {
    *s = '\0' ;
    s += 2 ;
  }
  else if( *s == '\n' ) *s++ = '\0' ;
  else return 0 ;

  b = io->r ;
  io->r = s ;

  return b ;
}

int spocp_send_results( conn_t *conn )
{
  return send_results( conn ) ;
}

/* ---------------------------------------------------------------------- */

int conn_writen( conn_t *ct, char *str, size_t n )
{
  int    nleft, nwritten ;
  char   *sp ;
  fd_set wset ;
  int    retval ;
  struct timeval to ;
  int    fd = ct->fd ;

  /* can happen during testing; it's hard to write to stdin so
     just fake it */

  if( fd == STDIN_FILENO ) {
    octet_t oct ;

    oct.val = str ;
    oct.len = n ;

   /*
    tmp = oct2strdup( &oct, '\\' ) ;
    traceLog( "REPLY: [%s]", tmp ) ;
    free( tmp ) ;
   */

    return( n ) ;
  }

  errno = 0 ;

  /* wait max 3 seconds */
  to.tv_sec = 3 ;
  to.tv_usec = 0 ;

  sp = str ;
  nleft = n ;
  FD_ZERO(&wset) ;
  while( nleft > 0 ) {
    FD_SET(fd,&wset) ;
    retval = select(fd+1,NULL,&wset,NULL,&to) ;

    if( retval ) {
      if(( nwritten = write(fd, sp, nleft)) <= 0 ) { ;
        if( errno == EINTR ) nwritten = 0  ;
        else return -1 ;
      }
      
      nleft -= nwritten ;
      sp += nwritten ;
    }
    else { /* timed out */
      break ;
    }
  }

  /* fdatasync( fd ) ; */

  return(n) ;
}                  

/* ---------------------------------------------------------------------- */

int conn_readn( conn_t *ct, char *str, size_t max )
{
  fd_set rset ;
  int retval, n = 0 ;
  struct timeval to ;
  int    fd = ct->fd ;

  errno = 0 ;

  /* wait max 0.1 seconds */
  to.tv_sec = 0 ;
  to.tv_usec = 100000 ;

  FD_ZERO(&rset) ;

  FD_SET(fd,&rset) ;
  retval = select(fd+1,&rset,NULL,NULL,&to) ;

  if( retval ) {
    if(( n = read(fd, str, max)) <= 0 ) {
      if( errno == EINTR || errno == 0 ) n = 0 ;
      else return -1 ;
    }
  }
  else { /* timed out */
    ;
  }

  /* fdatasync( fd ) ; */

  return(n) ;
}

/* ---------------------------------------------------------------------- */

int spocp_conn_read( conn_t *conn )
{
  int n ;
  spocp_iobuf_t *io = conn->in ;

  pthread_mutex_lock( &io->lock ) ;

  n = conn->readn( conn, io->w, io->left ) ;

  if( n > 0 ) {
    io->left -= n ;
    io->w += n ;
    *io->w = '\0' ;
  }

  pthread_mutex_unlock( &io->lock ) ;

  return n ;
}

int spocp_conn_write( conn_t *conn )
{
  int n, l ;
  spocp_iobuf_t *io = conn->out ;

  pthread_mutex_lock( &io->lock ) ;

  n = conn->writen( conn, io->r, io->w - io->r ) ;

  if( n >= 0 ) {
    LOG( SPOCP_DEBUG ) traceLog( "spocp_io_write wrote %d bytes", n ) ;

    io->r += n ;
    io->left -= n ;

    /* have to unlock since iobuf_shift will try to lock */
    pthread_mutex_unlock( &io->lock ) ;
    iobuf_shift( io ) ;

    if(( l = iobuf_content( io )) == 0 ) {
      DEBUG( SPOCP_DSRV ) traceLog( "Wrote everything in buffer" ) ; 
      pthread_cond_broadcast( &io->empty ) ;
    }
    else 
      DEBUG( SPOCP_DSRV ) traceLog( "%d left in buffert", l ) ; 
  }
  else 
    pthread_mutex_unlock( &io->lock ) ;

  if( 0 ) timestamp( "Done writing" ) ;
  return n ;
}

/* ---------------------------------------------------------------------- */

int send_results( conn_t *conn )
{
  spocp_iobuf_t *out = conn->out ;
  size_t         len ;
  char           ldef[16] ;
  int            nr ;

  if(0) timestamp( "Send results" ) ;
  len = out->w - out->r ;

  nr = snprintf( ldef, 16, "%d:", len ) ;
  iobuf_insert( out, 0, ldef, nr ) ;

  len += nr ;

  LOG( SPOCP_INFO ) {
    *out->w = '\0' ; 
    traceLog( "[%d]SEND_RESULT: [%s]", conn->fd, out->r) ;
  }

  if(0) timestamp( "Result placed in outqueue" ) ;

  return 1;
}

