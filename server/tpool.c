/***************************************************************************
                           tpool.c  -  description
                             -------------------
    begin                : Wed Jan 9 2002
    copyright            : (C) 2002 by Roland Hedberg
    email                : roland@catalogix.se
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>

#include <func.h>
#include <srv.h>
#include <wrappers.h>

void *tpool_thread(void *);

#define DEF_QUEUE_SIZE  16

typedef struct _ptharg {
  tpool_t *tpool ;
  int      id ;
} ptharg_t ;

int add_work_structs( pool_t *pool, int n )
{
  int i ;
  void *tw ;
  pool_item_t  *pi ;

  for( i = 0 ; i < n ; i++ ) {
    tw = Calloc( 1, sizeof( work_info_t )) ;
    pi = ( pool_item_t *) Calloc( 1, sizeof( pool_item_t )) ;
    pi->info = tw ;
  
    pool_add( pool, pi ) ;
  }

  return i ;
}

void free_work_struct( void *vp )
{
  if( vp ) {
    free( vp ) ;
  }
}

void free_wpool( afpool_t *afp )
{
  pool_t *pool ;
  pool_item_t *pi ;

  if( afp ) {
    if( afp->active ) { /* shouldn't be any actives */
      free( afp->active ) ;
    }
    if( afp->free ) {
      pool = afp->free ;

      while(( pi = pool_pop( pool ))) {
        free_work_struct( pi->info ) ;
        free( pi ) ;
      }

      free( pool ) ;
    }

    free( afp ) ;
  }
}

/*!
 * Create the initial set of threads, initialize them and add them to the free
 * pool. 
 * \param wthread Number of work threads to start with
 * \param max_queue_size This is as far as the workpool size is allowed to grow
 * \param do_not_block_when_full If no work thread is free one can either wait or
 *        so something like signal that server is too busy.
 * \return A pointer to the thread pool
 */
tpool_t *tpool_init( int wthreads, int max_queue_size, int do_not_block_when_full )
{
  int      i ;
  tpool_t *tpool = 0 ;
  ptharg_t ptharg  ;
   
  /* allocate a pool data structure */ 
  tpool = (tpool_t *) Malloc (sizeof(tpool_t)) ;

  /* initialize th fields */
  tpool->num_threads = wthreads;
  tpool->max_queue_size = max_queue_size;
  tpool->do_not_block_when_full = do_not_block_when_full;

  /* there is a lower limit to who small the pool can be */
  if( max_queue_size < DEF_QUEUE_SIZE ) tpool->cur_queue_size = max_queue_size ;
  else tpool->cur_queue_size = DEF_QUEUE_SIZE ;

  tpool->queue = afpool_new() ;

  add_work_structs( tpool->queue->free , tpool->cur_queue_size ) ;

  tpool->queue_closed = 0;  
  tpool->shutdown = 0; 

  /* traceLog("Creating %d threads", wthreads ) ; */

  tpool->threads = (pthread_t *) Calloc ( wthreads, sizeof(pthread_t)) ;

  pthread_cond_init(&(tpool->queue_not_empty), NULL) ;
  pthread_cond_init(&(tpool->queue_not_full), NULL) ;
  pthread_cond_init(&(tpool->queue_empty), NULL) ;

  ptharg.tpool = tpool ;

  /* create threads */
  for (i = 0; i != wthreads; i++) {
    ptharg.id = i ;
    pthread_create( &(tpool->threads[i]), NULL, tpool_thread, (void *) &ptharg ) ;
  }

  return tpool;
}

int tpool_add_work( tpool_t *tpool, proto_op *routine, conn_t *c )
{
  work_info_t *workp ;
  pool_item_t *pi ;

  if(0) timestamp( "tpool_add_work" ) ;

  /* traceLog( "queued items: %d", number_of_active( tpool->queue )) ; */

  /* traceLog( "queue size: %d, max_size: %d", tpool->cur_queue_size,
             tpool->max_queue_size)  ; */

  /* queue full and at maxsize but not locked */
  while( ( afpool_free_item( tpool->queue ) == 0 ) && 
      ( tpool->cur_queue_size == tpool->max_queue_size ) && 
      (!(tpool->shutdown || tpool->queue_closed)) ) {
 
    traceLog( "current queue_size=%d", tpool->cur_queue_size ) ;

    /* and this caller doesn't want to wait */
    if ( tpool->do_not_block_when_full ) {
      return -1;
    }

    /* wait for a struct to be free */
    pthread_cond_wait(&(tpool->queue_not_full), &(tpool->queue->aflock)) ;
  }

  /* the pool is in the process of being destroyed */
  if (tpool->shutdown || tpool->queue_closed) {
    return -1;
  }

  /* Any free workp structs around ? */
  if( afpool_free_item( tpool->queue ) == 0 ) { /* NO ! */
    traceLog( "No free workp structs !! " ) ;
    if( tpool->cur_queue_size < tpool->max_queue_size ) {
      if( tpool->cur_queue_size + DEF_QUEUE_SIZE < tpool->max_queue_size ) 
        add_work_structs( tpool->queue->free, DEF_QUEUE_SIZE ) ;
      else 
        add_work_structs( tpool->queue->free, tpool->max_queue_size - tpool->cur_queue_size ) ;
    }
    else { /* shouldn't happen */
      traceLog( "Reached the max_queue_size" ) ;
      return -1 ;
    }
  }

  pi = afpool_get_empty( tpool->queue ) ;

  workp = ( work_info_t * ) pi->info ;

  workp->routine = routine;
  workp->conn = c ;

  afpool_push_item( tpool->queue, pi ) ;

  if(0) timestamp("Signal the workitem" ) ; 
  pthread_cond_signal(&(tpool->queue_not_empty)) ;

  return 1;
}

int tpool_destroy( tpool_t *tpool, int finish )
{
  int           i ;

  afpool_lock(tpool->queue) ;

  /* Is a shutdown already in progress? */
  if (tpool->queue_closed || tpool->shutdown) {
    afpool_unlock(tpool->queue) ;


    return 0;
  }

  tpool->queue_closed = 1;

  /* If the finish flag is set, wait for workers to
     drain queue */
  if (finish == 1) {
    while (tpool->cur_queue_size != 0) {
      pthread_cond_wait(&(tpool->queue_empty), &(tpool->queue->aflock)) ;
    }
  }

  tpool->shutdown = 1;

  afpool_unlock(tpool->queue) ;

  /* Wake up any workers so they recheck shutdown flag */
  pthread_cond_signal(&(tpool->queue_not_empty)) ;
  pthread_cond_signal(&(tpool->queue_not_full)) ;


  /* Wait for workers to exit */
  for(i=0; i < tpool->num_threads; i++) {
    pthread_join(tpool->threads[i],NULL) ;
  }

  /* Now free pool structures */
  free(tpool->threads);

  free_wpool( tpool->queue ) ;

  free(tpool);

  return 1 ;
}

/* where the threads are spending their time */

void *tpool_thread(void *arg)
{
  ptharg_t      *ptharg = (ptharg_t *) arg ;
  tpool_t       *tpool ;
  work_info_t	*my_workp;
  int           res, id ;
  pool_item_t   *pi ;
  afpool_t      *workqueue ;

  tpool = ptharg->tpool ;
  id = ptharg->id ;
  workqueue = tpool->queue ;

  for(;;) {

    /* Get the queue lock */
    afpool_lock( workqueue ) ; 

    /* if(1) traceLog( "Thread %d looking for work", id ) ; */

    /* nothing in the queue, wait for something to appear */
    while ( afpool_active_item( workqueue ) == 0 && (!tpool->shutdown)) {
      /* cond_wait unlocks aflock on entering, locks it on return */
      pthread_cond_wait(&(tpool->queue_not_empty), &(workqueue->aflock)) ;
      /*traceLog("%d awaken", id ) ;
      timestamp("Any work around" ) ; */
    }

    afpool_unlock(workqueue) ;

    /* if(1) traceLog( "Thread %d working", id ) ; */

    /* Has a shutdown started while I was sleeping? */
    if (tpool->shutdown == 1) {
      pthread_exit(NULL);
    }

    /* Get to work, dequeue the next item */

    pi = afpool_get_item( workqueue ) ;

    /* traceLog( "Active workitems %d", number_of_active( workqueue )) ; */

    my_workp = ( work_info_t * ) pi->info ;

    /* Handle waiting destroyer threads *
    if( afpool_active_items( afp ) == 0 && 
      afpool_cond_signal_empty( afp )) ;
    */

    /* traceLog( "Doing some work" ) ; */

    /* Do this work item */
    res = (*(my_workp->routine))(my_workp->conn);

    wake_listener( 1 ) ;

    /* If error get a clean sheat */
    if( res == -1 ) iobuf_flush( my_workp->conn->in ) ; 

    /* if(1) timestamp( "workitem done" ) ; */

    /* returned to free list */
    my_workp->conn = 0 ;
    my_workp->routine = 0 ;
    
    /*if( 1 ) timestamp( "Return work item" ) ; */

    afpool_push_empty( workqueue, pi ) ; 

    /* tell the world there are room for more work */
    if ((!tpool->do_not_block_when_full) &&
        ( workqueue->free->size == 1 )) {

      pthread_cond_signal(&(tpool->queue_not_full)) ;
    }

    if( 0 ) timestamp( "loopturn done" ) ;
  } 

  return(NULL);            
}