/******************************************************************************
    (c) 2001 Patrick Caulfield                 patrick@debian.org

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
******************************************************************************/

#include <stdio.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <list>
#include <map>
#include <queue>
#include <string>
#include <strstream>
#include <iterator>
#include <iomanip>

#include "lat.h"
#include "utils.h"
#include "session.h"
#include "serversession.h"
#include "clientsession.h"
#include "lloginsession.h"
#include "portsession.h"
#include "connection.h"
#include "circuit.h"
#include "latcpcircuit.h"
#include "server.h"
#include "services.h"
#include "lat_messages.h"
#include "dn_endian.h"


// Create a server connection
LATConnection::LATConnection(int _num, unsigned char *buf, int len,
			     int _interface,
			     unsigned char _seq,
			     unsigned char _ack,
			     unsigned char *_macaddr):
    num(_num),
    interface(_interface),
    keepalive_timer(0),
    last_sequence_number(_ack),
    last_ack_number(_seq),
    queued_slave(false),
    eightbitclean(false),
    connected(false),
    role(SERVER)
{
    memcpy(macaddr, (char *)_macaddr, 6);
    int  ptr = sizeof(LAT_Start);
    LAT_Start *msg = (LAT_Start *)buf;

    debuglog(("New connection: (c: %x, s: %x)\n",
             last_sequence_number, last_ack_number));
    
    get_string(buf, &ptr, servicename);
    get_string(buf, &ptr, remnode);

    debuglog(("Connect from %s (LAT %d.%d) for %s, window size: %d\n",
	     remnode, msg->latver, msg->latver_eco, servicename, msg->exqueued));

    remote_connid = msg->header.local_connid;
    next_session = 1;
    max_window_size = msg->exqueued+1;
    max_window_size = 1; // All we can manage
    window_size = 0;
    lat_eco = msg->latver_eco;

    // I don't think this is actually true (TODO CHECK!)
    // but it seems to work.
    if (max_window_size < 4)
	max_slots_per_packet = 1;
    else
	max_slots_per_packet = 4;
    
    memset(sessions, 0, sizeof(sessions));
}

// Create a client connection
LATConnection::LATConnection(int _num, const char *_service,
			     const char *_portname, const char *_lta, 
			     const char *_remnode, bool queued, bool clean):
    num(_num),
    keepalive_timer(0),
    last_sequence_number(0xff),
    last_ack_number(0xff),
    queued(queued),
    queued_slave(false),
    eightbitclean(clean),
    connected(false),
    connecting(false),
    role(CLIENT)
{
    debuglog(("New client connection for %s created\n", _remnode));
    memset(sessions, 0, sizeof(sessions));
    strcpy((char *)servicename, _service);
    strcpy((char *)portname, _portname);
    strcpy((char *)remnode, _remnode);
    strcpy(lta_name, _lta);

    max_slots_per_packet = 1; // TODO: Calculate
    max_window_size = 1;      // Gets overridden later on.
    window_size = 0 ;
    next_session = 1;
}


bool LATConnection::process_session_cmd(unsigned char *buf, int len, 
					unsigned char *macaddr)
{
    int  msglen;
    unsigned int command;
    int  i;
    int  newsessionnum;
    int  ptr = sizeof(LAT_Header);
    LATSession *newsession;    
    LAT_SessionCmd *msg = (LAT_SessionCmd *)buf;  
    int num_replies = 0;
    LAT_SlotCmd reply[4];
    bool replyhere = false;
    
    debuglog(("process_session_cmd: %d slots, %d bytes\n",
             msg->header.num_slots, len));

    window_size--;
    if (window_size < 0) window_size=0;

    // For duplicate checking
    unsigned char saved_last_sequence_number = last_sequence_number;
    unsigned char saved_last_message_acked   = last_message_acked;

    last_sequence_number = msg->header.ack_number;
    last_message_acked   = msg->header.sequence_number;

#ifdef REALLY_VERBOSE_DEBUGLOG  
    debuglog(("MSG:      seq: %d,        ack: %d\n", 
	      msg->header.sequence_number, msg->header.ack_number));

    debuglog(("PREV:last seq: %d,   last ack: %d\n", 
	      last_sent_sequence, last_ack_number));
#endif

    // Is this a duplicate?
    if (saved_last_sequence_number == last_sequence_number &&
	saved_last_message_acked == last_message_acked)
    {
	debuglog(("Duplicate packet received...ignoring it\n"));
	return false;
    }

    // No blocks? just ACK it (if we're a server)
    if (msg->header.num_slots == 0)
    {
	if (role == SERVER) 
	{
	    reply[0].remote_session = msg->slot.local_session;
	    reply[0].local_session = msg->slot.remote_session;
	    reply[0].length = 0;
	    reply[0].cmd = 0;
	    replyhere = true;
	}

	LAT_SlotCmd *slotcmd = (LAT_SlotCmd *)(buf+ptr);
	unsigned char credits = slotcmd->cmd & 0x0F;
	
	debuglog(("No data: cmd: %d, credit: %d\n", msg->header.cmd, credits));
	
	LATSession *session = sessions[slotcmd->local_session];
	if (credits)
	{	    
	    if (session) session->add_credit(credits);
	}       
	if (replyhere && session && session->get_remote_credit() < 1)
	{
	    reply[0].cmd |= 15; // Add credit
	    session->inc_remote_credit(15);
	}
    }
    else
    {
        for (i=0; i<msg->header.num_slots && ptr<len; i++)
	{
	    LAT_SlotCmd *slotcmd = (LAT_SlotCmd *)(buf+ptr);
	    msglen  = slotcmd->length;
	    command = slotcmd->cmd & 0xF0;
	    unsigned char credits = slotcmd->cmd & 0x0F;

	    debuglog(("process_slot_cmd(%d:%x). command: %x, credit: %d, len: %d\n",
		     i, ptr, command, credits, msglen));
	    	    
	    ptr += sizeof(LAT_SlotCmd);

	    // Process the data.
	    LATSession *session = sessions[slotcmd->local_session];
	    if (session && credits) session->add_credit(credits);

	    switch (command)
	    {
	    case 0x00:
            {
                if (session)
                {
                    if (session->send_data_to_process(buf+ptr, msglen))
                    {
                        // No echo.
                        if (role == SERVER) 
			{
			    reply[num_replies].remote_session = slotcmd->local_session;
			    reply[num_replies].local_session = slotcmd->remote_session;
			    reply[num_replies].length = 0;
			    reply[num_replies].cmd = 0;
//			    num_replies++;
			    replyhere = true;
			}
                    }
                    // We are expecting an echo - don't send anything now
		    // but still increment the remote credit if the other end
		    // has run out.
		    debuglog(("Remote credit is %d\n", session->get_remote_credit()));
		    if (session->get_remote_credit() <= 2)
		    {
			reply[num_replies].remote_session = slotcmd->local_session;
			reply[num_replies].local_session = slotcmd->remote_session;
			reply[num_replies].length = 0;
			reply[num_replies].cmd = 15; // Just credit
			num_replies++;
			session->inc_remote_credit(15);
		    }
                }
                else
                {
                    // An error - send a disconnect.
		    reply[num_replies].remote_session = slotcmd->local_session;
		    reply[num_replies].local_session = slotcmd->remote_session;
		    reply[num_replies].length = 0;
		    reply[num_replies].cmd = 0xD3; // Invalid slot recvd
		    num_replies++;
                }
            }
            break;
	  
	    case 0x90:
		if (role == SERVER)
		{
		    int queued_connection;
		    if (is_queued_reconnect(buf, len, &queued_connection))
		    {
			master_conn = LATServer::Instance()->get_connection(queued_connection);
			if (!master_conn)
			{
			    debuglog(("Got queued reconnect for non-existant request ID\n"));

			    // Not us mate...
			    reply[num_replies].remote_session = slotcmd->local_session;
			    reply[num_replies].local_session = slotcmd->remote_session;
			    reply[num_replies].length = 0;
			    reply[num_replies].cmd = 0xD7; // No such service
			    num_replies++;
			}
			else
			{
			    // Connect a new port session to it
			    ClientSession *cs = (ClientSession *)master_conn->sessions[1];

			    newsessionnum = next_session_number();
			    newsession = new PortSession(*this,
							 (LAT_SessionStartCmd *)buf,
							 cs,
							 slotcmd->remote_session, 
							 newsessionnum, 
							 master_conn->eightbitclean);
			    if (newsession->new_session(remnode, "","",
							credits) == -1)
			    {
				newsession->send_disabled_message();
				delete newsession;
			    }
			    else
			    {
				sessions[newsessionnum] = newsession;
			    }
			}
		    }
		    else
		    {
			//  Check service name is one we recognise.
			ptr = sizeof(LAT_SessionStartCmd);
			unsigned char name[256];
			get_string(buf, &ptr, name);
			
			if (!LATServer::Instance()->is_local_service((char *)name))
			{
			    reply[num_replies].remote_session = slotcmd->local_session;
			    reply[num_replies].local_session = slotcmd->remote_session;
			    reply[num_replies].length = 0;
			    reply[num_replies].cmd = 0xD7; // No such service
			    num_replies++;
			}
			else
			{
			    newsessionnum = next_session_number();
			    newsession = new ServerSession(*this,
							   (LAT_SessionStartCmd *)buf,
							   slotcmd->remote_session, 
							   newsessionnum, false);
			    if (newsession->new_session(remnode, "", "",
							credits) == -1)
			    {
				newsession->send_disabled_message();
				delete newsession;
			    }
			    else
			    {
				sessions[newsessionnum] = newsession;
			    }
			}
		    }
		}
		else // CLIENT
		{
		    if (session)
			((ClientSession *)session)->got_connection(slotcmd->remote_session);
		}    
		break;

	    case 0xa0:
		// Data_b message - port information
		if (session)
		    session->set_port((unsigned char *)slotcmd);

		reply[num_replies].remote_session = slotcmd->local_session;
		reply[num_replies].local_session = slotcmd->remote_session;
		reply[num_replies].length = 0;
		reply[num_replies].cmd = 0;
		num_replies++;
		break;
	  
	    case 0xb0:
                // Attention. Force XON, Abort etc.
	        break;
		

	    case 0xc0:  // Reject - we will get disconnected
		debuglog(("Reject code %d: %s\n", credits,
			  lat_messages::session_disconnect_msg(credits)));
		// Deliberate fall-through.

	    case 0xd0:  // Disconnect
		if (session) session->disconnect_session(credits);
		if (queued_slave)
		{
		    queued_slave = false;
		}

		// If we have no sessions left then disconnect
		if (num_clients() == 0)
		{
		    reply[num_replies].remote_session = slotcmd->local_session;
		    reply[num_replies].local_session = slotcmd->remote_session;
		    reply[num_replies].length = 0;
		    reply[num_replies].cmd = 0xD1;/* No more slots on circuit */
		    num_replies++;
		}
		break;	  

	    default:
		debuglog(("Unknown slot command %x found. length: %d\n",
                         command, msglen));
		replyhere=true;
		break;
	    }
	    ptr += msglen;
	    if (ptr%2) ptr++; // Word-aligned
        }
    }

    // If "Response Requested" set, then make sure we send one.
    if (msg->header.cmd & 1 && !replyhere) 
    {
	debuglog(("Sending response because we were told to (%x)\n", msg->header.cmd));
	replyhere = true;
    }
    last_ack_number = last_message_acked;
 
    // Send any replies
    if (replyhere || num_replies)
    {
	debuglog(("Sending %d slots in reply\n", num_replies));
	unsigned char replybuf[1600];
	LAT_Header *header  = (LAT_Header *)replybuf;
	ptr = sizeof(LAT_Header);
	
	header->cmd       = LAT_CCMD_SREPLY;
	header->num_slots = num_replies;
	if (role == CLIENT) header->cmd |= 2; // To Host

	if (num_replies == 0) num_replies = 1;
	for (int i=0; i < num_replies; i++)
	{
	    memcpy(replybuf + ptr, &reply[i], sizeof(LAT_SlotCmd));
	    ptr += sizeof(LAT_SlotCmd); // Already word-aligned
	}

	send_message(replybuf, ptr, REPLY);
	return true;
    }
    
    return false;
}


void LATConnection::send_connect_ack()
{
    unsigned char reply[1600];
    int ptr;
    LAT_StartResponse *response = (LAT_StartResponse *)reply;
    LATServer *server = LATServer::Instance();

    // Send response...
    response->header.cmd       = LAT_CCMD_CONACK;
    response->header.num_slots = 0;
    response->maxsize          = dn_htons(1500);
    response->latver           = LAT_VERSION;
    response->latver_eco       = LAT_VERSION_ECO;
    response->maxsessions      = 254;
    response->exqueued         = 0;
    response->circtimer        = LATServer::Instance()->get_circuit_timer();
    response->keepalive        = LATServer::Instance()->get_keepalive_timer();
    response->facility         = dn_htons(0);
    response->prodtype         = 3;   // Wot do we use here???
    response->prodver          = 3;   // and here ???

    ptr = sizeof(LAT_StartResponse);
    add_string(reply, &ptr, server->get_local_node());
    add_string(reply, &ptr, remnode);
    add_string(reply, &ptr, (unsigned char*)"LAT for Linux");
    reply[ptr++] = '\0';
    
    send_message(reply, ptr, DATA);
    keepalive_timer = 0;
}

// Send a message on this connection NOW
int LATConnection::send_message(unsigned char *buf, int len, send_type type)
{
    LAT_Header *response = (LAT_Header *)buf;
    
    if (type == DATA)  
    {
	last_sequence_number++;
	retransmit_count = 0;
	last_sent_sequence = last_sequence_number;
	window_size++;
	debuglog(("send_message, window_size now %d\n", window_size));
	need_ack = true;
    }

    if (type == REPLY) 
    {
	last_sequence_number++;
	need_ack = false;
    }

    response->local_connid    = num;
    response->remote_connid   = remote_connid;
    response->sequence_number = last_sequence_number;
    response->ack_number      = last_ack_number;

    debuglog(("Sending message for connid %d (seq: %d, ack: %d, needack: %d)\n", 
	      num, last_sequence_number, last_ack_number, (type==DATA) ));

    keepalive_timer = 0;

    if (type == DATA) last_message = pending_msg(buf, len, (type==DATA) );
    return LATServer::Instance()->send_message(buf, len, interface, macaddr);
}


// Send a message on this connection when we can
int LATConnection::queue_message(unsigned char *buf, int len)
{
    LAT_Header *response = (LAT_Header *)buf;
    
    response->local_connid    = num;
    response->remote_connid   = remote_connid;
    response->sequence_number = last_sequence_number;
    response->ack_number      = last_ack_number;

    debuglog(("Queued messsge for connid %d\n", num));
    
    pending.push(pending_msg(buf, len, true));
    return 0;
}


// Enque a slot message on this connection
void LATConnection::send_slot_message(unsigned char *buf, int len)
{
    slots_pending.push(slot_cmd(buf,len));
}

LATConnection::~LATConnection()
{
    debuglog(("LATConnection dtor: %d\n", num));

    // Do we need to notify our master?
    if (queued_slave)
    {
	ClientSession *cs = (ClientSession *)master_conn->sessions[1];
	if (cs) cs->restart_pty();
    }
    else
    {
	// Delete all server sessions
	for (unsigned int i=1; i<MAX_SESSIONS; i++)
	{
	    if (sessions[i])
	    {
		delete sessions[i];
		sessions[i] = NULL;
	    }
	}
    }
// Send a "shutting down" message to the other end
    
}

// Generate the next session ID
int LATConnection::next_session_number()
{
    unsigned int i;
    for (i=next_session; i < MAX_SESSIONS; i++)
    {
	if (!sessions[i])
	{
	    next_session = i+1;
	    return i;
	}
    }

// Didn't find a slot here - try from the start 
    for (i=1; i < next_session; i++)
    {
	if (!sessions[i])
	{
	    next_session = i+1;
	    return i;
	}
    }
    return -1;
}


//
//  Send queued packets
//
void LATConnection::circuit_timer(void)
{

    // Increment keepalive timer - timer is measured in the same units
    // as the circut timer(100ths/sec) but the keepalive timer in the Server 
    // is measured in seconds.

    // Of course, we needn't send keepalive messages when we are a
    // disconnected client.
    if (role == SERVER ||
	(role == CLIENT && connected))
    {
	keepalive_timer += LATServer::Instance()->get_circuit_timer();
	if (keepalive_timer > LATServer::Instance()->get_keepalive_timer()*100 )
	{
	    // Send an empty message that needs an ACK.
	    // If we don't get a response to this then we abort the circuit.
	    debuglog(("keepalive timer expired: %d: limit: %d\n", keepalive_timer,
		      LATServer::Instance()->get_keepalive_timer()*100));
	    
	    // If we get into this block then there is no chance that there is
	    // an outstanding ack (or if there is then it's all gone horribly wrong anyway
	    // so it's safe to just send a NULL messae out.
	    // If we do exqueued properly this may need revisiting.
	    unsigned char replybuf[1600];
	    LAT_SessionReply *reply  = (LAT_SessionReply *)replybuf;
	    
	    reply->header.cmd          = LAT_CCMD_SESSION;
	    reply->header.num_slots    = 0;
	    reply->slot.remote_session = 0;
	    reply->slot.local_session  = 0;
	    reply->slot.length         = 0;
	    reply->slot.cmd            = 0;
	    
	    if (role == CLIENT) reply->header.cmd = LAT_CCMD_SESSION | 2;
	    
	    send_message(replybuf, sizeof(LAT_SessionReply), DATA);
	    return;
	}
    }

    // Did we get an ACK for our last message?
    if (need_ack && last_sequence_number != last_sent_sequence)
    {
	if (++retransmit_count > LATServer::Instance()->get_retransmit_limit())
	{
	    debuglog(("hit retransmit limit on connection %d\n", num));
	    need_ack = false;

	    unsigned char buf[1600];
	    LAT_Header *header = (LAT_Header *)buf;
	    int ptr=sizeof(LAT_Header);
	    
	    header->cmd             = LAT_CCMD_DISCON;
	    header->num_slots       = 0;
	    header->local_connid    = num;
	    header->remote_connid   = remote_connid;
	    header->sequence_number = last_sequence_number;
	    header->ack_number      = last_ack_number;
	    buf[ptr++] = 0x06; // Retransmission limit reached.
	    LATServer::Instance()->send_message(buf, ptr, interface, macaddr);

	    LATServer::Instance()->delete_connection(num);
	    
	    // Mark this node as unavailable in the service list
	    LATServices::Instance()->remove_node(std::string((char *)remnode));
	    return;
	}	
	debuglog(("Last message not ACKed: RESEND\n"));
	last_message.send(interface, macaddr);
	return;
    }
    else
    {
	retransmit_count = 0;
	need_ack = false;
    }

    // Poll our sessions
    if (role == SERVER)
    {
	for (unsigned int i=0; i<MAX_SESSIONS; i++)
	{
	    if (sessions[i])
		sessions[i]->read_pty();
	}
    }
    
    // Coalesce pending messages and queue them
    while (!slots_pending.empty())
    {
	debuglog(("circuit Timer:: slots pending = %d\n", slots_pending.size()));
        unsigned char buf[1600];
        LAT_Header *header = (LAT_Header *)buf;
        int len=sizeof(LAT_Header);
	if (role == SERVER)
	    header->cmd         = LAT_CCMD_SDATA;
	else
	    header->cmd         = LAT_CCMD_SESSION;
        header->num_slots       = 0;
        header->local_connid    = num;
        header->remote_connid   = remote_connid;
        header->sequence_number = last_sequence_number;
        header->ack_number      = last_ack_number;

	// Send as many slot data messages as we can
	while ( (header->num_slots < max_slots_per_packet && !slots_pending.empty()))
        {
            header->num_slots++;
          
            slot_cmd &cmd(slots_pending.front());

            memcpy(buf+len, cmd.get_buf(), cmd.get_len());
            len += cmd.get_len();
            if (len%2) len++;// Keep it on even boundary
          
            slots_pending.pop();
        }
	if (header->num_slots)
	{
	    debuglog(("Sending %d slots on circuit timer\n", header->num_slots));
	    pending.push(pending_msg(buf, len, true));
	}
    }

#ifdef VERBOSE_DEBUG
    if (!pending.empty())
    {
      debuglog(("Window size: %d, max %d\n", window_size, max_window_size));
    }
#endif

    //  Send a pending message (if we can)
    if (!pending.empty() && window_size < max_window_size)
    {
        // Send the top message
        pending_msg &msg(pending.front());
      
        last_sequence_number++;
        last_sent_sequence = last_sequence_number;
        need_ack = msg.needs_ack();
	retransmit_count = 0;
	
        LAT_Header *header      = msg.get_header();
        header->sequence_number = last_sequence_number;
        header->ack_number      = last_ack_number;
     
        debuglog(("Sending message on circuit timer: seq: %d, ack: %d (need_ack: %d)\n",
		  last_sequence_number, last_ack_number, need_ack));
 
        msg.send(interface, macaddr);
	last_message = msg; // Save it in case it gets lost on the wire;
        pending.pop();
	window_size++;
	keepalive_timer = 0;
    }
}


void LATConnection::remove_session(unsigned char id)
{
    debuglog(("Deleting session %d\n", id));
    if (sessions[id])
    {
        delete sessions[id];
	sessions[id] = NULL;
    }

// TODO: Disconnect & Remove connection if no sessions active...
    if (num_clients() == 0)
    {
//  //	send_disconnect_error(3, msg???, interface, macaddr)
	LATServer::Instance()->delete_connection(num);
    }
}

// Initiate a client connection
int LATConnection::connect(ClientSession *session)
{
   // Look up the service name.
    std::string node;
    int  this_int=0;

    // Are we in the middle of actually connecting ?
    if (connecting) return 0;

    // Only connect if we are the first session,
    // else just initiate slot connect.
    if (!connected)
    {
	// If no node was specified then just use the highest rated one
	if (remnode[0] == '\0')
	{
	    if (!LATServices::Instance()->get_highest(std::string((char*)servicename),
						      node, macaddr, &this_int))
	    {
		debuglog(("Can't find service %s\n", servicename));
		// Tell the user
		session->disconnect_session(7);
		return -2; // Never eard of it!
	    }
	    strcpy((char *)remnode, node.c_str());
	}
	else
	{
	    // Try to find the node
	    if (!LATServices::Instance()->get_node(std::string((char*)servicename),
						   std::string((char*)remnode), macaddr, &this_int))
	    {
		debuglog(("Can't find node %s in service\n", remnode, servicename));
		
		// Tell the user
		session->disconnect_session(7);		
		return -2; // Never eard of it!
	    }
	}
	
	// Reset the sequence & ack numbers
	last_sequence_number = 0xff;
	last_ack_number = 0xff;
	remote_connid = 0;
	interface = this_int;
	connecting = true;
	
	// Queued connection or normal?
	if (queued)
	{
	    debuglog(("Requesting connect to queued service\n"));
	    
	    int ptr;
	    unsigned char buf[1600];
	    LAT_Command *msg = (LAT_Command *)buf;
	    ptr = sizeof(LAT_Command);
	    
	    msg->cmd         = LAT_CCMD_COMMAND;
	    msg->format      = 0;
	    msg->hiver       = LAT_VERSION;
	    msg->lover       = LAT_VERSION;
	    msg->latver      = LAT_VERSION;
	    msg->latver_eco  = LAT_VERSION_ECO;
	    msg->maxsize     = dn_htons(1500);
	    msg->request_id  = num;
	    msg->entry_id    = 0;
	    msg->opcode      = 2; // Request Queued connection
	    msg->modifier    = 1; // Send status periodically
	    
	    add_string(buf, &ptr, remnode);
	    
	    buf[ptr++] = 32; // Groups length
	    memcpy(buf + ptr, LATServer::Instance()->get_user_groups(), 32);
	    ptr += 32;
	    
	    add_string(buf, &ptr, LATServer::Instance()->get_local_node());
	    buf[ptr++] = 0; // ASCIC source port
	    add_string(buf, &ptr, (unsigned char *)"LAT for Linux");
	    add_string(buf, &ptr, servicename);
	    add_string(buf, &ptr, portname);
	    
	    // Send it raw.
	    return LATServer::Instance()->send_message(buf, ptr, interface, macaddr);
	}
	else
	{
	    int ptr;
	    unsigned char buf[1600];
	    LAT_Start *msg = (LAT_Start *)buf;
	    ptr = sizeof(LAT_Start);
	    
	    debuglog(("Requesting connect to service on interface %d\n", interface));
	    
	    msg->header.cmd          = LAT_CCMD_CONNECT;
	    msg->header.num_slots    = 0;
	    msg->header.local_connid = num;  
	    
	    msg->maxsize     = dn_htons(1500);
	    msg->latver      = LAT_VERSION;
	    msg->latver_eco  = LAT_VERSION_ECO;
	    msg->maxsessions = 254;
	    msg->exqueued    = 0;
	    msg->circtimer   = LATServer::Instance()->get_circuit_timer();
	    msg->keepalive   = LATServer::Instance()->get_keepalive_timer();
	    msg->facility    = dn_htons(0); // Eh?
	    msg->prodtype    = 3;   // Wot do we use here???
	    msg->prodver     = 3;   // and here ???
	    
	    add_string(buf, &ptr, remnode);
	    add_string(buf, &ptr, LATServer::Instance()->get_local_node());
	    add_string(buf, &ptr, (unsigned char *)"LAT for Linux");
	    
	    return send_message(buf, ptr, LATConnection::DATA);
	}
    }
    else
    {
	// Just do a session connect.
	session->connect();
    }
    return 0;
}

int LATConnection::create_client_session(char *service, char *port)
{
// Create a ClientSession
    int newsessionnum = next_session_number();
    LATSession *newsession = new ClientSession(*this, 0,
					       newsessionnum, lta_name, 
					       eightbitclean);
    if (newsession->new_session(remnode, service, port, 0) == -1)
    {
	delete newsession;
	return -1;
    }
    sessions[newsessionnum] = newsession;
    return 0;
}

void LATConnection::got_status(unsigned char *node, LAT_StatusEntry *entry)
{
    debuglog(("Got status %d from node %s, queue pos = %d,%d. session: %d\n", 
	      entry->status, node, entry->max_que_pos, entry->min_que_pos,
	      entry->session_id));

// Check this is OK - status session ID seems to be rubbish
    if (role == CLIENT && sessions[1])
    {
	ClientSession *s = (ClientSession *)sessions[1];
	s->show_status(node, entry);
    }
}

int LATConnection::create_llogin_session(int fd, char *service, char *port)
{
// Create an lloginSession
    int newsessionnum = next_session_number();

    LATSession *newsession = new lloginSession(*this, 0, newsessionnum, 
					       lta_name, fd);
    if (newsession->new_session(remnode, service, port, 0) == -1)
    {
	delete newsession;
	return -1;
    }
    sessions[newsessionnum] = newsession;
    return 0;
}

int LATConnection::got_connect_ack(unsigned char *buf)
{
    LAT_StartResponse *reply = (LAT_StartResponse *)buf;
    remote_connid = reply->header.local_connid;

    last_sequence_number = reply->header.ack_number;
    last_message_acked   = reply->header.sequence_number;
    last_ack_number = last_message_acked;
    connected = true;
    connecting = false;

    max_window_size = reply->exqueued+1;
    max_window_size = 1; // All we can manage

    debuglog(("got connect ack. seq: %d, ack: %d\n", 
	      last_sequence_number, last_message_acked));

// Start clientsessions
    for (unsigned int i=1; i<MAX_SESSIONS; i++)
    {
	ClientSession *cs = (ClientSession *)sessions[i];
	if (cs && cs->waiting_start())
	{
	    cs->connect();
	}
    }
    return 0;
}

// Called when the client needs disconnecting
int LATConnection::disconnect_client()
{
// Reset all clients - remote end has disconnected the connection
    for (unsigned int i=0; i<MAX_SESSIONS; i++)
    {
	ClientSession *cs = (ClientSession *)sessions[i];
	if (cs)
	{
	    cs->restart_pty();
	}
    }
    return 0;
}


// Extract "parameter 2" from the packet. If there is one then
// it means this is a connection from the terminal server for
// a queued *client* port so we...
// do "something clever..."
bool LATConnection::is_queued_reconnect(unsigned char *buf, int len, int *conn)
{
    int ptr = sizeof(LAT_SessionCmd)+3;
    
    ptr += buf[ptr]+1; // Skip over destination service
    if (ptr >= len) return false;

    ptr += buf[ptr]+1; // Skip over source service
    if (ptr >= len) return false;

// Do parameters -- look for a 2
    while (ptr < len)
    {
	int param_type = buf[ptr++];
	if (param_type == 2)
	{
	    ptr++; //Skip over parameter length (it's 2)
	    unsigned short param = dn_ntohs(*(unsigned short *)(buf+ptr));

	    debuglog(("found Parameter 2: request ID is %d\n", param));
	    *conn = param;
	    queued_slave = true;
	    return true;
	}
	else
	{
	    ptr += buf[ptr]+1; // Skip over it
	}
    }
    return false;
}

int LATConnection::pending_msg::send(int interface, unsigned char *macaddr)
{
    return LATServer::Instance()->send_message(buf, len, interface, macaddr);
}

int LATConnection::num_clients()
{
    unsigned int i;
    int num = 0;
    
    for (i=1; i<MAX_SESSIONS; i++)
	if (sessions[i]) num++;

    return num;
}


void LATConnection::show_client_info(bool verbose, std::ostrstream &output)
{
    if (role == SERVER) return; // No client info for servers!

    // Only show llogin ports if verbose is requested.
    if (!verbose && strcmp(lta_name, "llogin")==0) return;

    output << lta_name << std::setw(24-strlen((char*)lta_name)) << " " << servicename
	   << std::setw(16-strlen((char*)servicename)) << " " 
	   << remnode << std::setw(16-strlen((char*)remnode)) << " " << portname
	   << std::setw(16-strlen((char*)portname)) << " " << (queued?"Yes":"No ") 
	   << (eightbitclean?" 8":" ") << std::endl;
}
