/******************************************************************************
    (c) 2000 Patrick Caulfield                 patrick@pandh.demon.co.uk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#include <sys/types.h>
#include <unistd.h>

#include <strstream>
#include <list>
#include <string>
#include <map>
#include <queue>
#include <iterator>

#include "lat.h"
#include "latcp.h"
#include "utils.h"
#include "services.h"
#include "session.h"
#include "connection.h"
#include "latcpcircuit.h"
#include "server.h"


LATCPCircuit::LATCPCircuit(int _fd):
    fd(_fd),
    state(STARTING)
{
}


LATCPCircuit::~LATCPCircuit()
{
}

bool LATCPCircuit::do_command()
{
    unsigned char head[3];
    bool retval = true;

    debuglog(("latcp: do_command on fd %d\n", fd));
    
    // Get the message header (cmd & length)
    if (read(fd, head, sizeof(head)) != 3)
	return false; // Bad header
    
    int len = head[1] * 256 + head[2];
    unsigned char *cmdbuf = new unsigned char[len];

    // Read the message buffer
    if (read(fd, cmdbuf, len) != len)
    {
	delete[] cmdbuf;
	return false; // Bad message
    }

    // Have we completed negotiation? 
    if (head[0] != LATCP_VERSION &&
	state != RUNNING)
    {
	delete[] cmdbuf;
	return false;
    }
    
    debuglog(("latcp: do_command %d\n", head[0]));
    
    // Do the command
    switch (head[0])
    {
    case LATCP_VERSION:
	if (strcmp(VERSION, (char*)cmdbuf) == 0)
	{
	    state = RUNNING; // Versions match
	    send_reply(LATCP_VERSION, VERSION, -1);
	}
	else
	{
	    debuglog(("Connect from invalid latcp version %s\n", cmdbuf));
	    send_reply(LATCP_ERRORMSG, "latcp version does not match latd version " VERSION, -1);
	    retval = false;
	}
	break;
    case LATCP_SHOWSERVICE:
    {
	int verbose = cmdbuf[0];
	ostrstream st;

	debuglog(("latcp: show_services(verbose=%d)\n", verbose));

	LATServices::Instance()->list_services(verbose?true:false, st);
	send_reply(LATCP_SHOWSERVICE, st.str(), st.pcount());
    }
    break;

    case LATCP_SHOWCHAR:
    {
	int verbose = cmdbuf[0];
	ostrstream st;

	debuglog(("latcp: show_characteristics(verbose=%d)\n", verbose));

	LATServer::Instance()->show_characteristics(verbose?true:false, st);
	send_reply(LATCP_SHOWCHAR, st.str(), st.pcount());
    }
    break;

    
    case LATCP_SETRESPONDER:
    {
	bool onoff = cmdbuf[0]==0?false:true;
	LATServer::Instance()->SetResponder(onoff);
    }
    break;
    
    case LATCP_UNLOCK:
    {
	debuglog(("UNLOCK received...off we go\n"));
	LATServer::Instance()->unlock();

    }
    break;

    
    // Add a login service
    case LATCP_ADDSERVICE:
    {
	unsigned char name[255];
	unsigned char ident[255];
	bool static_rating;
	int  rating;
	int  ptr=2;

	static_rating = (bool)cmdbuf[0];
	rating = cmdbuf[1];
	get_string((unsigned char*)cmdbuf, &ptr, name);
	get_string((unsigned char*)cmdbuf, &ptr, ident);

	debuglog(("latcp: add service: %s (%s)\n",
		  name, ident));

	LATServer::Instance()->add_service((char*)name, (char*)ident, rating, static_rating);
	send_reply(LATCP_ACK, "", -1);
    }
    break;

    // Delete service
    case LATCP_REMSERVICE:
    {
	unsigned char name[255];
	int  ptr=0;

	get_string((unsigned char*)cmdbuf, &ptr, name);

	debuglog(("latcp: del service: %s\n", name));

	if (LATServer::Instance()->remove_service((char*)name))
	    send_reply(LATCP_ACK, "", -1);
	else
	    send_reply(LATCP_ERRORMSG, "Local service does not exist", -1);
    }
    break;

    case LATCP_REMPORT:
    {
	unsigned char name[255];
	int  ptr=0;

	get_string((unsigned char*)cmdbuf, &ptr, name);

	debuglog(("latcp: del service: %s\n", name));

	if (LATServer::Instance()->remove_port((char*)name))
	    send_reply(LATCP_ACK, "", -1);
	else
	    send_reply(LATCP_ERRORMSG, "Local port does not exist", -1);
    }
    break;

    // Set the multicast timer
    case LATCP_SETMULTICAST:
    {
	int newtimer;

	newtimer = *(int *)cmdbuf;

	debuglog(("latcp: Set multicast: %d\n", newtimer));

	LATServer::Instance()->set_multicast(newtimer);
	send_reply(LATCP_ACK, "", -1);
    }
    break;

    // Set the retransmit limit
    case LATCP_SETRETRANS:
    {
        int newlim;

        newlim = *(int *)cmdbuf;

        debuglog(("latcp: Set retransmit limit: %d\n", newlim));

        LATServer::Instance()->set_retransmit_limit(newlim);
        send_reply(LATCP_ACK, "", -1);
    }
    break;

    // Set the keepalive timer
    case LATCP_SETKEEPALIVE:
    {
        int newtimer;

        newtimer = *(int *)cmdbuf;

        debuglog(("latcp: Set keepalive timer: %d\n", newtimer));

        LATServer::Instance()->set_keepalive_timer(newtimer);
        send_reply(LATCP_ACK, "", -1);
    }
    break;


    // Connect a port to a remote service
    case LATCP_ADDPORT:
    {
	unsigned char service[255];
	unsigned char remport[255];
	unsigned char localport[255];
	unsigned char remnode[255];
	int  ptr=0;

	get_string((unsigned char*)cmdbuf, &ptr, service);
	get_string((unsigned char*)cmdbuf, &ptr, remport);
	get_string((unsigned char*)cmdbuf, &ptr, localport);
	get_string((unsigned char*)cmdbuf, &ptr, remnode);
	bool queued = cmdbuf[ptr++];

	debuglog(("latcp: add port: %s:%s (%s)\n",
		  service, remport, localport));

	if (LATServer::Instance()->make_client_connection(service, 
							  remport,
							  localport,
							  remnode,
							  queued) < 0)
	{
	    debuglog(("sending failure back to LATCP\n"));
	    send_reply(LATCP_ERRORMSG, "Error creating client service, service unknown", -1);
	}
	else
	{
	    send_reply(LATCP_ACK, "", -1);
	}
    }
    break;


    case LATCP_SHUTDOWN:
    {
	LATServer::Instance()->Shutdown();
    }
    break;

    default:
	retval = false;
	break;
    }

    delete[] cmdbuf;
    return retval;
}

bool LATCPCircuit::send_reply(int cmd, char *buf, int len)
{
    char outhead[3];

    if (len == -1) len=strlen(buf)+1;
    
    outhead[0] = cmd;
    outhead[1] = len/256;
    outhead[2] = len%256;
    write(fd, outhead, 3);
    write(fd, buf, len);

    // TODO Error checking.
    return true;
}
