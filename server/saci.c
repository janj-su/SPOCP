#include "locl.h"
#include <sexptool.h>

#define SACI 1

/*
typedef char   *(argfunc) (conn_t * r);

typedef struct {
	char           *var;
	argfunc        *af;
	char            type;
	char            dynamic;
} arg_t;
*/

static sexpargfunc get_path;

static sexpargfunc get_action;
static sexpargfunc get_arguments;
static sexpargfunc get_ip;
static sexpargfunc get_host;
static sexpargfunc get_inv_host;
static sexpargfunc get_subject;
#ifdef HAVE_SSL
static sexpargfunc get_ssl_vers;
static sexpargfunc get_ssl_cipher;
static sexpargfunc get_ssl_subject;
static sexpargfunc get_ssl_issuer;
#endif
#ifdef HAVE_SASL
static sexpargfunc get_sasl_mech;
static sexpargfunc get_sasl_username;
static sexpargfunc get_sasl_authuser;
static sexpargfunc get_sasl_ssf;
#endif
static sexpargfunc get_transpsec;


const sexparg_t           transf[] = {
	{"ip", get_ip, 'a', FALSE},
	{"host", get_host, 'a', FALSE},
	{"invhost", get_inv_host, 'l', FALSE},
	{"subject", get_subject, 'l', FALSE},
	{"operation", get_action, 'l', TRUE},
	{"arguments", get_arguments, 'l', TRUE},
	{"path", get_path, 'l', TRUE},
#ifdef HAVE_SSL
	{"ssl_vers", get_ssl_vers, 'a', FALSE},
	{"ssl_cipher", get_ssl_cipher, 'a', FALSE},
	{"ssl_subject", get_ssl_subject, 'a', FALSE},
	{"ssl_issuer", get_ssl_issuer, 'a', FALSE},
#endif
#ifdef HAVE_SASL
	{"sasl_mech", get_sasl_mech, 'a', FALSE},
	{"sasl_username", get_sasl_username, 'a', FALSE},
	{"sasl_authuser", get_sasl_authuser, 'a', FALSE},
	{"sasl_ssf", get_sasl_ssf, 'a', TRUE},
#endif
	{"transportsec", get_transpsec, 'l', FALSE}
};

int             ntransf = (sizeof(transf) / sizeof(transf[0]));

#ifdef HAVE_SSL

#define TPSEC_X509	0
#define TPSEC_X509_WCC	1
#define TPSEC_KERB	2
#define TPSEC_SASL  3

char           *tpsec[] = {
	"(12:TransportSec(4:vers%{ssl_vers})(12:chiphersuite%{ssl_cipher}))",
	"(12:TransportSec(4:vers%{ssl_vers})(12:chiphersuite%{ssl_cipher})(7:autname4:X509(7:subject%{ssl_subject})(6:issuer%{ssl_issuer})))",
	"(12:TransportSec(4:vers%{kerb_vers})(7:autname8:kerberos%{kerb_realm}%{kerb_localpart}))",
	"(12:TransportSec(9:mechanism%{sasl_mech})(2:id(8:username%{sasl_username})(8:authuser%{sasl_authuser}))(3:ssf%{sasl_ssf}))"
};

sexparg_t	**tpsec_X509;
sexparg_t	**tpsec_X509_wcc;
sexparg_t   **tpsec_sasl;

#endif

char	*srvquery =
    "(6:server(2:ip%{ip})(4:host%{host})%{transportsec})";
char	*operquery =
    "(9:operation%{operation}(6:server(2:ip%{ip})(4:host%{host})%{transportsec}))";
char	*rulequery =
		"(8:spocpaci(8:argument%{arguments})(9:operation%{operation})(7:subject%{subject}))";

sexparg_t	**srvq;
sexparg_t	**operq;
sexparg_t	**ruleq;

/* #define AVLUS 1 */

/*
 * ---------------------------------------------------------------------- 
 */

/*
 * static void argt_free( sexparg_t **arg ) { int i ;
 * 
 * if( arg ) { for( i = 0 ; arg[i] || arg[i]->type != 0 ; i++ ) { if(
 * arg[i]->var ) free( arg[i]->var ) ; free( arg[i] ) ; } if( arg[i] ) free(
 * arg[i] ) ;
 * 
 * free( arg ) ; } } 
 */

/*
 * ---------------------------------------------------------------------- 
 */

static char    *
get_ip(void *arg)
{
	return ((( work_info_t *) arg)->conn->sri.hostaddr);
}

static char    *
get_host(void *arg)
{
	return ((( work_info_t *) arg)->conn->sri.hostname);
}

static char    *
get_subject(void * vp)
{
	char            sexp[512], *sp;
	unsigned int    size = 512;
	conn_t          *conn = (( work_info_t * ) vp)->conn ;

	if (conn->sri.sexp_subject == 0) {
		sp = sexp_printv(sexp, &size, "o", conn->sri.subject);
		if (sp)
			conn->sri.sexp_subject = Strdup(sexp);
	}

	return (conn->sri.sexp_subject);
}

static char    *
get_inv_host(void * vp)
{
	char          **arr, format[16], list[512], *sp;
	int             n, i, j;
	unsigned int    size = 512;
	conn_t          *conn = (( work_info_t * ) vp)->conn ;

	if (conn->sri.invhost)
		return conn->sri.invhost;

	arr = line_split(conn->sri.hostname, '.', 0, 0, 0, &n);

	for (i = 0, j = n; i < (n + 1) / 2; i++, j--) {
		if (i == j)
			break;
		sp = arr[i];
		arr[i] = arr[j];
		arr[j] = sp;
	}

	for (i = 0; i <= n; i++)
		format[i] = 'a';
	format[i] = '\0';

	sexp_printa(list, &size, format, (void **) arr);

	conn->sri.invhost = Strdup(list);

	return (conn->sri.invhost);
}

static char *
get_path(void * vp)
{
	char           *res, *rp;
	unsigned int    size;
	work_info_t	*wi = (work_info_t *) vp;

	if (wi->oppath == 0)
		return 0;

	size = wi->oppath->len + 9;

	res = (char *) Malloc(size * sizeof(char));

	rp = sexp_printv(res, &size, "o", wi->oppath);

	if (rp == 0)
		Free(res);

	return rp;
}

static char    *
get_action(void * vp)
{
	char           *res, *rp;
	work_info_t	*wi = (work_info_t *) vp;
	unsigned int    size = wi->oper.len + 9;

	/*
	 * traceLog(LOG_DEBUG,"Get_action") ; 
	 */

	res = (char *) Malloc(size * sizeof(char));

	rp = sexp_printv(res, &size, "o", &wi->oper);

	if (rp == 0)
		Free(res);

	return rp;
}

static char    *
get_arguments(void * vp)
{
	char           *res, *rp, format[32];
	unsigned int    size;
	int             i;
	work_info_t	*wi = (work_info_t *) vp;

	if (wi->oparg == 0)
		return 0;

	for (i = 0, size = 0; i < wi->oparg->n; i++) {
		size += wi->oparg->arr[i]->len + 5;
		format[i] = 'o';
	}
	format[i] = '\0';

	res = (char *) Malloc(size * sizeof(char));

	rp = sexp_printa(res, &size, format, (void **) wi->oparg->arr);

	if (rp == 0)
		Free(res);

	return rp;
}

#ifdef HAVE_SSL
static char    *
get_ssl_vers(void * arg)
{
	return ((( work_info_t *) arg)->conn->ssl_vers);
}

static char    *
get_ssl_cipher(void * c)
{
	return ((( work_info_t * ) c)->conn->cipher);
}

static char    *
get_ssl_subject(void * c)
{
	return ((( work_info_t *) c)->conn->subjectDN);
}

static char    *
get_ssl_issuer(void * c)
{
	return ((( work_info_t *) c)->conn->issuerDN);
}
#endif

#ifdef HAVE_SASL
static char    *
get_sasl_mech(void * c)
{
	return ((( work_info_t *) c)->conn->sasl_mech);
}

static char    *
get_sasl_username(void * c)
{
	return ((( work_info_t *) c)->conn->sasl_username);
}

static char    *
get_sasl_authuser(void * c)
{
	return ((( work_info_t *) c)->conn->sasl_authuser);
}

static char    *
get_sasl_ssf(void * c)
{
	char *new_ssf = Calloc(1, 20);
	snprintf(new_ssf, 19, "%d", *((work_info_t *) c)->conn->sasl_ssf);
	return(new_ssf);
}
#endif

/*
 * ---------------------------------------------------------------------- 
 */

static char    *
get_transpsec(void * vp)
{
	/*
	 * XXX fixa SPOCP_LAYER-jox h�r 
	 */
	conn_t *conn= (( work_info_t * ) vp)->conn ;
#ifdef HAVE_SSL

	if (conn->ssl != NULL) {
		if (conn->transpsec == 0) {
			if (conn->subjectDN)
				conn->transpsec =
				    sexp_constr(vp, tpsec_X509_wcc);
			else
				conn->transpsec =
				    sexp_constr(vp, tpsec_X509);
		}
		return conn->transpsec;
	} else
#endif
#ifdef HAVE_SASL
		if(conn->sasl != NULL) {
			if (conn->transpsec == 0)
				if (conn->sasl_ssf)
					conn->transpsec = sexp_constr(vp, tpsec_sasl);
			return conn->transpsec;
		}
#endif
		return "";

}

/*
 * ---------------------------------------------------------------------- 
 * ---------------------------------------------------------------------- 
 */

void
saci_init(void)
{
	/* sexparg_t **tp_sec_kerb ; 
	 */

#ifdef HAVE_SSL
	tpsec_X509 = parse_format(tpsec[TPSEC_X509], transf, ntransf);
	if (tpsec_X509 == 0)
		traceLog(LOG_ERR,"Could not parse TPSEC_X509");
	else
		LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,"Parsed TPSEC_X509 OK");

	tpsec_X509_wcc = parse_format(tpsec[TPSEC_X509_WCC], transf, ntransf);
	if (tpsec_X509_wcc == 0)
		traceLog(LOG_ERR,"Could not parse TPSEC_X509_WCC");
	else
		LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,"Parsed TPSEC_X509_WCC OK");
#endif
#ifdef HAVE_SASL
	tpsec_sasl = parse_format(tpsec[TPSEC_SASL], transf, ntransf);
	if (tpsec_sasl == 0)
		traceLog(LOG_ERR,"Could not parse TPSEC_SASL");
	else
		LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,"Parsed TPSEC_SASL OK");
#endif
	/*
	 * tpsec_kerb = parse_format( tpsec[TPSEC_KERB], transf ) ; 
	 */

	srvq = parse_format(srvquery, transf, ntransf);
	if (srvq == 0)
		traceLog(LOG_ERR,"*ERR* Could not parse srvquery");
	else
		LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,"Parsed srvquery OK");

	operq = parse_format(operquery, transf, ntransf);
	if (operq == 0)
		traceLog(LOG_ERR,"*ERR* Could not parse operquery");
	else
		LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,"Parsed operquery OK");

	ruleq = parse_format(rulequery, transf, ntransf);
	if (ruleq == 0)
		traceLog(LOG_ERR,"*ERR* Could not parse rulequery");
	else
		LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,"Parsed rulequery OK");

}

/*
 * ---------------------------------------------------------------------- 
 */

static spocp_result_t
spocp_access(work_info_t *wi, sexparg_t ** arg, octet_t *path)
{
	ruleset_t		*rs = wi->conn->rs;
	spocp_result_t	res = SPOCP_DENIED;	/* the default */
#ifdef SACI
	octet_t		oct;
	char		*sexp;
	element_t	*ep = 0;
	resset_t	*rset = 0;
	comparam_t	comp; 
	octarr_t    *on = 0;
	checked_t   *cr=0;
#endif

	/* 
	 * If I'm running on a unix domain socket I implicitly trust
	 * processes on that machine
	 */
	if (wi->conn->srv->uds) return SPOCP_SUCCESS;

	/*
	 * no ruleset or rules means nothing is allowed 
	 */
	if (rs == 0 || rules(rs->db) == 0) {
#ifdef AVLUS
		traceLog(LOG_ERR,"No rules to tell me what to do");
#endif
		return res;
	}

#ifdef AVLUS
	oct_print(LOG_DEBUG,"Looking for ruleset (spocp_access)", path);
#endif

	/*
	 * No ruleset means nothing is allowed !!! 
	 */
	if ((rs = ruleset_find(path, rs)) == 0 || rs->db == 0) {
#ifdef AVLUS
		traceLog(LOG_DEBUG,"No ruleset");
#endif

		return res;
	}
	/*
	 * The same if there is no rules in the ruleset 
	 */
	if (rs->db->ri->rules == 0) {
#ifdef AVLUS
		traceLog(LOG_DEBUG,"No rules in the ruleset");
#endif
		return res;
	}

#ifdef SACI
#ifdef AVLUS
	traceLog(LOG_DEBUG,"Making the internal access query");
#endif
	sexp = sexp_constr(wi, arg);

	LOG(SPOCP_DEBUG) traceLog(LOG_DEBUG,
		"Internal access Query: \"%s\" in \"%s\"", sexp, rs->name);

	oct_assign(&oct, sexp);

	if ((res = element_get(&oct, &ep)) != SPOCP_SUCCESS) {
		traceLog(LOG_ERR,"The S-expression \"%s\" didn't parse OK", sexp);

		Free(sexp);
		return res;
	}

	if (oct.len) {		/* shouldn't be anything left*/
		Free(sexp);
		element_free(ep);
		return SPOCP_DENIED;
	}

	memset(&comp, 0, sizeof(comparam_t));

	comp.rc = SPOCP_SUCCESS;
	comp.head = ep;
       comp.blob = &on;
       comp.cr = &cr;

	res = allowed(rs->db->jp, &comp, &rset);

	Free(sexp);
	element_free(ep);
	resset_free( rset );
	if ( *(comp.cr))
		checked_free( *(comp.cr) );

	return res;
#else
	return SPOCP_SUCCESS;
#endif

}

spocp_result_t
server_access(conn_t * con)
{
	work_info_t	wi;
	char		path[MAXNAMLEN + 1];
	octet_t		oct;

	snprintf(path, MAXNAMLEN, "%s/server", localcontext);
	oct_assign(&oct, path);

	memset(&wi,0,sizeof(work_info_t));
	wi.conn = con;

	return spocp_access( &wi, srvq, &oct);
}

spocp_result_t
operation_access(work_info_t *wi)
{
	char            path[MAXNAMLEN + 1];
	octet_t			oct;
	spocp_result_t  r;

	snprintf(path, MAXNAMLEN, "%s/operation", localcontext);
	oct_assign(&oct, path);

	r = spocp_access(wi, operq, &oct);

	if (r != SPOCP_SUCCESS)
		traceLog(LOG_INFO,"Operation disallowed");

	return r;
}

spocp_result_t
rule_access(work_info_t *wi)
{
	spocp_result_t  r;

	r = spocp_access(wi, ruleq, wi->oppath);

	if (r != SPOCP_SUCCESS)
		traceLog(LOG_INFO,"Operation disallowed");

	return r;
}
