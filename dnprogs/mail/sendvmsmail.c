/******************************************************************************
    (c) 1998-2000 P.J. Caulfield               patrick@tykepenguin.cix.co.uk
    
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
////
// sendvmsmail.c
// VMSmail client for Linux.
//
// This program should be run from a pipe from a .forward or aliases file.
// see the README.mail for details
////

#define MAIL_OBJECT 27

#ifndef SENDMAIL_COMMAND
#define SENDMAIL_COMMAND "/usr/sbin/sendmail -oem"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdnet/dnetdb.h>
#include <netdnet/dn.h>
#include "configfile.h"

/* Prototypes */
int parse_header(char **to, char **subject, char **from, char **real_from);
int mail_error(char *to, char *name, char *subject, char *error);
int open_socket(char *node);
int send_to_vms(char *to, char *subject, char *from, char *reply_to);

int main(int argc, char *argv[])
{
    char *to       = NULL;
    char *subject  = NULL;
    char *from     = NULL;
    char *reply_to = NULL;

    openlog("sendvmsmail", LOG_PID, LOG_DAEMON);
    read_configfile();

    syslog(LOG_ERR, "running as %d\n", getuid());
    
    // Get the relevant fields from the mail header
    if (parse_header(&to, &subject, &from, &reply_to) == 0)
    {
	// OK, send the message to VMS - we get -1 as a return here for
	// general sockety type errors. Problems we can diagnose ourself
	// are handled by 'send_to_vms' and 0 is returned.
	if (send_to_vms(to, subject, from, reply_to) == -1)
	{
	    char err[256];
	    
	    sprintf(err, "Error sending to VMS system: %s\n", strerror(errno));
	    mail_error(reply_to, to, subject, err);
	    return 0;
	}
    }
    return 0;
}

// Reads the to, from and subject fields from the mail header.
// 'from' is the reformatted from field (ie the one we send to VMSmail) and
// 'real_from' is the field just as it comes from the message (we use this
// for mailing failure messages back).
// Note that 'Reply-To'(if present) always overrides 'From:' in a mail header.
//
// When this routine returns the file pointer is positioned at the start
// of the message body (ie it reads past the blank line).
int parse_header(char **to, char **subject, char **from, char **real_from)
{
    char  input_line[1024];
    int   got_replyto = 0;
    char  *error = NULL;

    do
    {
	fgets(input_line, sizeof(input_line), stdin);
	
	input_line[strlen(input_line)-1] = '\0';
//	syslog(LOG_INFO, "line: %s\n", input_line);
	
	if (strncmp(input_line, "Subject: ", 9) == 0)
	{
	    *subject = malloc(strlen(input_line));
	    strcpy(*subject, input_line+9+strspn(input_line+9, " "));
	}

	// Make FROM into a VMSMAIL address
	if (strncmp(input_line, "From: ", 6) == 0 && got_replyto == 0)
	{
	    *from = malloc(strlen(input_line));
	    *real_from = malloc(strlen(input_line));
	    sprintf(*from, "\"%s\"", input_line+6+strspn(input_line+6, " "));

	    // Save real FROM address in case of errors.
	    strcpy(*real_from, input_line+6+strspn(input_line+6, " "));
	}

	// reply-to overrides from:
	if (strncmp(input_line, "Reply-To: ", 11) == 0)
	{
	    *from = malloc(strlen(input_line)+strlen(config_hostname));
	    *real_from = malloc(strlen(input_line));
	    sprintf(*from, "%s::\"%s\"", config_hostname,
		    input_line+6+strspn(input_line+11, " "));

	    // Save real FROM address in case of errors.
	    strcpy(*real_from, input_line+6+strspn(input_line+6, " "));
	    got_replyto = 1;
	}

	if (strncmp(input_line, "To: ", 4) == 0)
	{
	    char *ptr;
	    char *endline;
	    
	    ptr=strchr(input_line, '(');
	    
	    if (ptr) // probably 'vmsmail@pandh (tramp::patrick)' format
	    {
		endline=strchr(input_line, ')');
		if (endline > ptr)
		{
		    *endline='\0';
		    ptr++;
		    // ptr should now be the address
		    if (strstr(ptr, "::") == NULL)
		    {
			error = "The address does not contain a VMS username in brackets.";
			continue;
		    }
		    *to = malloc(strlen(ptr)+1);
		    strcpy(*to, ptr);
		}
		else
		{
		    error = "The address does not contain a VMS username in brackets.";
		    continue;
		}
	    }
	    else // maybe it is '"tramp::patrick " <vmsmail@pandh>' format
	    {
		ptr=strchr(input_line, '<');
		if (ptr)
		{
		    int p;
		    
		    *ptr = '\0';
		    // Remove trailing spaces.
		    p = strlen(input_line)-1;
		    while (p && (input_line[p] == ' ' || input_line[p] == '"'))
			input_line[p--] = '\0';

		    ptr = input_line+4; // go past "To: "

		    if (strstr(ptr, "::") == NULL)
		    {
			error = "The address does not contain a VMS username outside angle brackets.";
			continue;
		    }
		    // Remove leading quote
		    if (*ptr == '"') ptr++;
		    *to = malloc(strlen(ptr)+1);
		    strcpy(*to, ptr);
		}
		else
		{
		    error = "The address does not contain a VMS username in the form node::user.";
		    continue;
		}
	    }
	}
    } while ((!feof(stdin)) && (input_line[0] != '\0'));

    // No subject is not valid to VMS so we may make one up.
    if (!*subject)
    {
	*subject = malloc(32);
	strcpy(*subject, "No subject");
    }
    
    // Make sure we have all the bits
    if (error == NULL && (*to==NULL || *from==NULL || *subject==NULL || *real_from==NULL) )
    {
	error="The message header was incomplete";
    }
    
    if (error)
    {
	mail_error(*real_from, *to, *subject, error);
	return -1;
    }
    
    return 0;
}

//
// Send an error message back to the originator
//
int mail_error(char *to, char *name, char *subject, char *error)
{
    char buf[256];
    FILE *mailpipe;
    
    // Open a pipe to sendmail.
    sprintf(buf, "%s", SENDMAIL_COMMAND);
    mailpipe = popen(buf, "w");
    if (mailpipe != NULL)
    {
	// Send the header
	fprintf(mailpipe, "From: VMS-Mail-forwarder (vmsmail@%s)\n",config_hostname);
	fprintf(mailpipe, "To: %s\n",to);
	fprintf(mailpipe, "Subject: Error in transmission\n");

	fprintf(mailpipe, "\n");
	
	fprintf(mailpipe, "Your message to '%s' could not be delivered to\n", name);
	fprintf(mailpipe, "VMS because of the following error:\n");
	fprintf(mailpipe, "\n");

	fprintf(mailpipe, "%s\n", error);
	fprintf(mailpipe, "\nThe subject of the message was '%s'\n", subject);

	pclose(mailpipe);
    }
    else
    {
	syslog(LOG_ERR, "Can't open pipe to sendmail: %m");
	return -1;
    }
  return 0;
}

//
// Open a socket to VMSmail
//
int open_socket(char *node)
{
    int                  sockfd;
    struct nodeent      *np;
    char                *local_user;
    int                  i;
    struct sockaddr_dn   sockaddr;
    struct accessdata_dn accessdata;
    
    if ((sockfd=socket(AF_DECnet,SOCK_SEQPACKET,DNPROTO_NSP)) == -1) 
    {
	return -1;
    }

    if ( (np=getnodebyname(node)) == NULL)
    {
	errno  = EHOSTUNREACH;
	return -1;
    }

    memset(&accessdata, 0, sizeof(accessdata));
    
    // Try very hard to get the local username
    local_user = getlogin();
    if (!local_user || local_user == (char *)0xffffffff)
	local_user = getenv("LOGNAME");
    if (!local_user) local_user = getenv("USER");
    if (local_user)
    {
	
	strcpy((char *)accessdata.acc_acc, local_user);
	accessdata.acc_accl = strlen((char *)accessdata.acc_acc);
	for (i=0; i<accessdata.acc_accl; i++)
	    accessdata.acc_acc[i] = toupper(accessdata.acc_acc[i]);
    }
    
    if (setsockopt(sockfd,DNPROTO_NSP,SO_CONACCESS,&accessdata,
		   sizeof(accessdata)) < 0)
    {
	 return -1;
    }
    sockaddr.sdn_family    = AF_DECnet;
    sockaddr.sdn_flags	   = 0x00;
    sockaddr.sdn_objnum	   = MAIL_OBJECT;
    sockaddr.sdn_objnamel  = 0x00;
    sockaddr.sdn_add.a_len = 0x02;
    memcpy(sockaddr.sdn_add.a_addr, np->n_addr,2);
    
    if (connect(sockfd, (struct sockaddr *)&sockaddr, 
		sizeof(sockaddr)) < 0) 
    {
	return -1;
    }
    
    return sockfd;
}

// Returns -1 if errno is to be reported as an error, 0 otherwise.
// NOTE: returning 0 does NOT mean the operation succeeded, just that
// no more error messages are to be sent.
int send_to_vms(char *to, char *subject, char *from, char *reply_to)
{
    int   sockfd;
    char  node[7];
    char  recvbuf[256];
    int   recvlen;
    char *vmsuser;

    syslog(LOG_INFO, "Sending message from %s to %s\n", reply_to, to);

    // Send message to VMS
    strncpy(node, to, 7); // Guarantee we have a colon in the name
    *(char *)strchr(node, ':') = '\0';
    sockfd = open_socket(node);
    if (sockfd < 0)
    {
	char err[256];

	sprintf(err, "Cannot connect to VMS system: %s\n", strerror(errno));
	mail_error(reply_to, to, subject, err);
	return 0;
    }

    // Local user -- VMS add nodename::
    if (write(sockfd, from, strlen(from)) < 0) return -1;
	
    vmsuser = strchr(to, ':') + 2; // VMS user
    if (write(sockfd, vmsuser, strlen(vmsuser)) < 0) return -1;
    
    recvlen = read(sockfd, recvbuf, sizeof(recvbuf));
    if (recvlen < 0) return -1;
    
    // Check for error - most likely an unknown user
    if (recvbuf[0] != '\001')
    {
	if (recvlen == 4) // Get the message text
	    recvlen = read(sockfd, recvbuf, sizeof(recvbuf));
	
	if (recvlen < 0) return -1;
	recvbuf[recvlen] = '\0';

	syslog(LOG_ERR, "Error returned from VMS after sending username: %s\n", recvbuf);
	mail_error(reply_to, to, subject, recvbuf);
	return 0;
    }
    
    if (write(sockfd, "\0", 1) < 0) return -1;
    if (write(sockfd, to, strlen(to)) < 0) return -1;
    if (write(sockfd, subject, strlen(subject)) < 0) return -1;

    // Send message text
    fgets(recvbuf, sizeof(recvbuf), stdin);
    while (!feof(stdin))
    {
	if (recvbuf[strlen(recvbuf)-1] == '\n')
	    recvbuf[strlen(recvbuf)-1] = '\0';

	// The socket layer doesn't like sending empty packets so we send
	// a space on its own instead.
	if (recvbuf[0] == '\0')
	{
	    recvbuf[0] = ' ';
	    recvbuf[1] = '\0';
	}
	if (write(sockfd, recvbuf, strlen(recvbuf)) < 0) return -1;

	// Get next line
	fgets(recvbuf, sizeof(recvbuf), stdin);
    }
    // send NUL to terminate the conversation
    if (write(sockfd, "\0", 1) < 0) return -1;

    // Check for error
    recvlen = read(sockfd, recvbuf, sizeof(recvbuf));
    if (recvbuf[0] != '\001')
    {
	if (recvlen == 4) // Get the message text
	    recvlen = read(sockfd, recvbuf, sizeof(recvbuf));

	if (recvlen < 0) return -1;
	recvbuf[recvlen] = '\0';
	
	syslog(LOG_ERR, "Error returned from VMS after sending message: %s\n", recvbuf);
	
	mail_error(reply_to, to, subject, recvbuf);
	return 0;
    }
    return 0;
}
