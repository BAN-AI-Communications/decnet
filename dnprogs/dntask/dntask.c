/******************************************************************************
    (c) 1998      P.J. Caulfield          patrick@tykepenguin.cix.co.uk
                  K.   Humborg            kenn@avalon.wombat.ie
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 ******************************************************************************
 */
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdnet/dn.h>
#include <netdnet/dnetdb.h>
#include <fcntl.h>
#include <ctype.h>

struct	sockaddr_dn		sockaddr;
static  struct	nodeent		*np;
static  char			node[20];
static  int			sockfd;
static  int                     timeout = 60;

struct	sockaddr_dn		sockaddr;
struct	accessdata_dn		accessdata;
static  struct	nodeent        *binadr;
static  char    		filename[128];
static	unsigned char		buf[32760];
static  int			sockfd;
static  char                   *lasterror;
static  int                     binary_mode;

/* DECnet phase IV limits */
#define MAX_NODE      6
#define MAX_USER     12
#define MAX_PASSWORD 12
#define MAX_ACCOUNT  12
#ifndef FALSE
#define TRUE 1
#define FALSE 0
#endif

static int parse(char *);
static void usage(FILE *);
/*-------------------------------------------------------------------------*/

/*
 * Run an interactive command procedure. As we get input from either the
 * remote or local end we pass it on to the other.
 */
void be_interactive(void)
{
    fd_set         in;
    struct timeval tv;
    int            len;

    FD_ZERO(&in);
    FD_SET(sockfd, &in);
    FD_SET(STDIN_FILENO, &in);
    tv.tv_sec  = timeout;
    tv.tv_usec = 0;

    /* Loop for input */    
    while ( select(sockfd+1, &in, NULL, NULL, (timeout==0?NULL:&tv)) > 0)
    {
	if (FD_ISSET(sockfd, &in)) // From VMS to us
	{
	    len = read(sockfd, buf, sizeof(buf));
	    if (len < 0)
	    {
	        perror("reading from VMS");
		return;
	    }

            if (binary_mode)
	    {
                write(STDOUT_FILENO, buf, len);
            }
	    else
	    {
	        buf[len] = '\0';
	        if (buf[len-1] == '\n') buf[len-1] = '\0';
		printf("%s\n", buf);
            }
	}
	if (FD_ISSET(STDIN_FILENO, &in)) // from us to VMS
	{
	    len = read(STDIN_FILENO, buf, sizeof(buf));
	    if (len < 0)
	    {
	        perror("reading from stdin");
		return;
	    }
	    if (len == 0) return; //EOF 
	    if (buf[len-1] == '\n') buf[--len] = '\0';
	    write(sockfd, buf, len);
	}
	
	// Reset for another select
	FD_ZERO(&in);
	FD_SET(sockfd, &in);
	FD_SET(STDIN_FILENO, &in);
	tv.tv_sec  = timeout;
	tv.tv_usec = 0;
    }
    fprintf(stderr, "Time-out expired\n");
}

/*
 * Just dump whatever arrives onto stdout/
 */
void print_output(void)
{
    int len;
    
    while ( ((len = read(sockfd, buf, sizeof(buf)))) )
    {
        if (len == -1)
	{
	    if (errno != ENOTCONN)  perror("Error reading from network");
	    break;
        }
	
        if (binary_mode)
	{
            write(STDOUT_FILENO, buf, len);
        }
	else
	{
	    buf[len] = '\0';
	    if (buf[len-1] == '\n') buf[len-1] = '\0';
	    printf("%s\n", buf);
        }
    }
}

/*
 * Open the connection.
 */
int setup_link(void)
{
    if ( (np=getnodebyname(node)) == NULL)
    {
	printf("Unknown node name %s\n",node);
	exit(0);
    }

/* Limit on the length of a DECnet object name */
    if (strlen(filename) > 16)
    {
        printf("task name must be less than 17 characters\n");
        exit(2);
    }

    if ((sockfd=socket(AF_DECnet,SOCK_SEQPACKET,DNPROTO_NSP)) == -1) 
    {
	perror("socket");
	exit(-1);
    }

    // If no taskname was given then use a default.
    if (filename[0] == '\0')
    {
	strcpy(filename, "TASK");
    }

    // Provide access control and proxy information
    if (setsockopt(sockfd,DNPROTO_NSP,SO_CONACCESS,&accessdata,
		   sizeof(accessdata)) < 0) 
    {
	perror("setsockopt");
	exit(-1);
    }

    /* Open up object number 0 with the name of the task */
    sockaddr.sdn_family   = AF_DECnet;

    /* Old kernel patches need flags to be 1 for a named object.
       The newer kernel patches define this as SDF_WILD so that's
       how we know
     */
    sockaddr.sdn_flags	  = 0x00;
    sockaddr.sdn_objnum	  = 0x00;
    memcpy(sockaddr.sdn_objname, filename, strlen(filename));
    sockaddr.sdn_objnamel = strlen(filename);
    memcpy(sockaddr.sdn_add.a_addr, np->n_addr,2);
    sockaddr.sdn_add.a_len = 2;

    
    if (connect(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) 
    {
	perror("connect");
	exit(-1);
    }
    return TRUE;
}

/*
 * Start here
 */
int main(int argc, char *argv[])
{
    int opt;
    int interactive = 0;

    if (argc < 2)
    {
        usage(stdout);
        exit(1);
    }

    binary_mode = FALSE;

/* Get command-line options */
    opterr = 0;
    optind = 0;
    while ((opt=getopt(argc,argv,"?hVibt:")) != EOF)
    {
	switch(opt)
	{
	case 'h': 
	    usage(stdout);
	    exit(1);

	case '?':
	    usage(stderr);
	    exit(1);
	    
	case 'i':
	    interactive = 1;
	    break;
	    
	case 't':
	    timeout = atoi(optarg);
	    break;
	    
	case 'V':
	    printf("\ndntask from dntools version %s\n\n", VERSION);
	    exit(1);
	    break;

        case 'b':
            binary_mode = TRUE;
            break;
	}
    }
    if (!argv[optind])
    {
	usage(stderr);
	exit(1);
    }
    
    /* Parse the command into its parts */
    if (!parse(argv[optind]))
    {
	fprintf(stderr, "%s\n", lasterror);
	exit(2);
    }

    if (!setup_link()) return 2;

/* Do the real work */
    if (!interactive)
	print_output();
    else
	be_interactive();
    
    close(sockfd);
    return 0;
}


/*-------------------------------------------------------------------------*/
/* Parse the filename into its component parts */
static int parse(char *fname)
{
    enum  {NODE, USER, PASSWORD, ACCOUNT, NAME, FINISHED} state;
    int   n0=0;
    int   n1=0;
    char *local_user;
    
    state = NODE; /* Node is mandatory */

    while (state != FINISHED)
    {
	switch (state)
	{
	case NODE:
	    if (fname[n0] != ':' && fname[n0] != '\"' && fname[n0] != '\'')
	    {
		if (n1 >= MAX_NODE || 
		    fname[n0] == ' ' || fname[n0] == '\n')
		{
		    lasterror = "File name parse error";
		    return FALSE;
		}
		node[n1++] = fname[n0++];
	    }
	    else
	    {
		node[n1] = '\0';
		n1 = 0;
		if (fname[n0] == '\"')
		{
		    n0++;
		    state = USER;
		}
		else
		{
		    n0 += 2;
		    state = NAME;
		}
	    }
	    break;

	case USER:
	    if (fname[n0] != ' ' && fname[n0] != '\"' && fname[n0] != '\'')
	    {
		if (n1 >= MAX_USER)
		{
		    lasterror = "File name parse error";
		    return FALSE;
		}
		accessdata.acc_user[n1++] = fname[n0++];
	    }
	    else
	    {
		accessdata.acc_user[n1] = '\0';
		n1 = 0;
		if (fname[n0] == ' ')
		{
		    state = PASSWORD;
		    n0++;
		}
		else /* Must be a quote */
		{
		    state = NAME;
		    /* Check for :: */
		    n0 += 3;
		}
	    }
	    break;

	case PASSWORD:
	    if (fname[n0] != ' ' && fname[n0] != '\"' && fname[n0] != '\'')
	    {
		if (n1 >= MAX_PASSWORD)
		{
		    lasterror = "File name parse error";
		    return FALSE;
		}
		accessdata.acc_pass[n1++] = fname[n0++];
	    }
	    else
	    {
		accessdata.acc_pass[n1] = '\0';
		n1 = 0;
		if (fname[n0] == ' ')
		{
		    n0++;
		    state = ACCOUNT;
		}
		else /* Must be a quote */
		{
		    state = NAME;
		    /* Check for :: */
		    n0 += 3;
		}
	    }
	    break;

	case ACCOUNT:
	    if (fname[n0] != '\'' && fname[n0] != '\"')
	    {
		if (n1 >= MAX_ACCOUNT)
		{
		    lasterror = "File name parse error";
		    return FALSE;
		}
		accessdata.acc_acc[n1++] = fname[n0++];
	    }
	    else
	    {
		accessdata.acc_acc[n1] = '\0';;
		state = NAME;
		n1 = 0;
		/* Check for :: */
		n0 += 3;
	    }
	    break;

	case NAME:
	    strcpy(filename, fname+n0);
	    state = FINISHED;
	    break;

	case FINISHED: // To keep the compiler happy
	    break;
	} /* switch */
    }

/* tail end validation */
    binadr = getnodebyname(node);
    if (!binadr)
    {
	lasterror = "Unknown or invalid node name ";
	return FALSE;
    }

    // Try very hard to get the local username
    local_user = getlogin();
    if (!local_user || local_user == (char *)0xffffffff)
      local_user = getenv("LOGNAME");
    if (!local_user) local_user = getenv("USER");
    if (local_user)
    {
        int i;

	strcpy((char *)accessdata.acc_acc, local_user);
	accessdata.acc_accl = strlen((char *)accessdata.acc_acc);
        for (i=0; i<accessdata.acc_accl; i++)
            accessdata.acc_acc[i] = toupper(accessdata.acc_acc[i]);
    }
    else
        accessdata.acc_acc[0] = '\0';


    /* Complete the accessdata structure */
    accessdata.acc_userl = strlen(accessdata.acc_user);
    accessdata.acc_passl = strlen(accessdata.acc_pass);
    accessdata.acc_accl  = strlen(accessdata.acc_acc);
    return TRUE;
}

static void usage(FILE *f)
{
    fprintf(f, "\nUSAGE: dntask [OPTIONS] 'node\"user password\"::commandprocedure'\n\n");
    fprintf(f, "NOTE: The VMS filename really should be in single quotes to\n");
    fprintf(f, "      protect it from the shell\n");

    fprintf(f, "\nOptions:\n");
    fprintf(f, "  -i           Interact with the command procedure\n");
    fprintf(f, "  -t           Timeout (in seconds) for interactive command procedure input\n");
    fprintf(f, "  -b           Treat received data as binary data\n");
    fprintf(f, "  -? -h        display this help message\n");
    fprintf(f, "  -V           show version number\n");

    fprintf(f, "\nExamples:\n\n");
    fprintf(f, " dntask 'myvax::'  - defaults to TASK.COM and proxy username\n");
    fprintf(f, " dntask -i 'clustr\"patrick thecats\"::do_dcl.com'\n");
    fprintf(f, "\n");
}
