/*
    protocol.cc from libdap

    Copyright (C) 1998-1999 Patrick Caulfield       patrick@tykepenguin.cix.co.uk

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <assert.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <regex.h>
#include <ctype.h>
#include <netdnet/dn.h>
#include <netdnet/dnetdb.h>

#include "dn_endian.h"
#include "logging.h"
#include "connection.h"
#include "protocol.h"

//---------------------------------- dap_bytes() ------------------------------
bool dap_bytes::read(dap_connection &c)
{
    char *b = c.getbytes(length);
    if (!b) return false;

    memcpy(value, b, length);
    return true;
}

bool dap_bytes::write(dap_connection &c)
{
    return c.putbytes(value, length);
}

unsigned char dap_bytes::get_byte(int bytenum)
{
    assert(bytenum < length);
    return value[bytenum];
}

char *dap_bytes::get_string()
{
    value[length] = '\0';
    return (char *)value;
}

unsigned short dap_bytes::get_short()
{
    int i;
    unsigned long s = 0;
    
    for (i=0; i<length; i++)
    {
	s |= value[i] << (i*8);
    }

    return (unsigned short)s;
}

unsigned int dap_bytes::get_int()
{
    int i;
    unsigned long s = 0;
    
    for (i=0; i<length; i++)
    {
	s |= value[i] << (i*8);
    }

    return (unsigned int)s;
}

void dap_bytes::set_byte(int bytenum, unsigned char newval)
{
    assert(length > bytenum);
    value[bytenum] = newval;
}

void dap_bytes::set_short(unsigned short newval)
{
    *(unsigned short *)value = dn_htons(newval);
    length = 2;
}

void dap_bytes::set_int(unsigned int newval)
{
    *(unsigned int *)value = dn_htonl(newval);
    length = 4;
}

void dap_bytes::set_string(const char *newval)
{
    length = strlen(newval);
    strcpy((char *)value, newval);
}
//---------------------------------- dap_ex() ------------------------------
bool dap_ex::read(dap_connection &c)
{
    int i=0;
    char *b;

    do
    {
	b = c.getbytes(1);
	if (!b) return false;

	value[i] = *b;
    }
    while((value[i++] & 0x80));
    length = i; // Set to real length;
    real_length = i;
    return true;
}

bool dap_ex::write(dap_connection &c)
{
    return c.putbytes(value, real_length);
}


bool dap_ex::get_bit(int bit)
{
    int bytenum;
    int bitnum;

    if (!length) return false;
    if (bit > length*7) return false;
    
    bytenum = bit / 7;
    bitnum  = bit % 7;
    
    return (value[bytenum]&(1<<bitnum))!=0;
}

void dap_ex::set_bit(int bit)
{
    int bytenum;
    int bitnum;

    bytenum = bit / 7;
    bitnum  = bit % 7;
    
    value[bytenum] |= (1<<bitnum);
    if (real_length <= bytenum) real_length = bytenum+1;
    
// Now set the top bits of all previous bytes
    for (int i=0; i<bytenum; i++)
    {
	value[i] |= 0x80;
    }
}

void dap_ex::clear_bit(int bit)
{
    int bytenum;
    int bitnum;

    bytenum = bit / 7;
    bitnum  = bit % 7;

    value[bytenum] &= ~(1<<bitnum);
}

unsigned char dap_ex::get_byte(int b)
{
    return value[b];
}

// Called by routines that know the exact format of the bits to be 
// sent and can't be arsed to set them bit-by-bit
void dap_ex::set_byte(int bytenum, unsigned char newval)
{
    assert(bytenum < length);

    value[bytenum] = newval;
    if (real_length <= bytenum) real_length = bytenum+1;
}
//-------------------------------dap_image-------------------------------------
bool dap_image::write(dap_connection &c)
{
    c.putbytes(&real_length, 1);
    return c.putbytes(value, real_length);
}

bool dap_image::read(dap_connection &c)
{
    char *b = c.getbytes(1);
    if (!b) return false;

    real_length = *b;

    b = c.getbytes(real_length);
    if (!b) return false;

    memcpy(value, b, real_length);
    return true;
}

unsigned char dap_image::get_byte(int bytenum)
{
    assert(bytenum < real_length);
    return value[bytenum];
}

char *dap_image::get_string()
{
    value[real_length] = '\0';
    return (char *)value;
}

unsigned short dap_image::get_short()
{
    int i;
    unsigned long s = 0;
    
    for (i=0; i<real_length; i++)
    {
	s |= value[i] << (i*8);
    }

    return (unsigned short)s;
}

unsigned int dap_image::get_int()
{
    int i;
    unsigned long s = 0;
    
    for (i=0; i<real_length; i++)
    {
	s |= value[i] << (i*8);
    }

    return (unsigned int)s;
}

void dap_image::set_int(unsigned int v)
{
    *(unsigned int *)value = dn_htonl(v);
    real_length = 4;
}

void dap_image::set_short(unsigned short v)
{
    *(unsigned short *)value = dn_htons(v);
    real_length = 2;
}

void dap_image::set_string(const char *s)
{
    strcpy((char *)value, s);
    real_length = strlen(s);
}

//-----------------------------------------------------------------------------
//-------------------------- DAP Messages start here --------------------------
//-----------------------------------------------------------------------------

// Send the message header. This header will have a single byte length
// so make sure the message is < 256 bytes long.
int dap_message::send_header(dap_connection &c)
{
    unsigned char head[] = {2, 0};
    c.putbytes(&msg_type, 1);

    if (c.verbosity() > 3)
	DAPLOG((LOG_INFO, "Sending message of type %s\n", type_name()));

    return c.putbytes(head, 2); // Tell connection to send length
}

// Send the message header. if 'header' is set then the connection class
// will add the length in when the message is complete.
int dap_message::send_header(dap_connection &c, bool header)
{
    unsigned char head[] = {2, 0};
    if (c.verbosity() > 2) 
	DAPLOG((LOG_INFO, "Sending message of type %s\n", type_name()));

    c.putbytes(&msg_type, 1);
    if (header)
    {
	return c.putbytes(head, 2); // Tell connection to send length
    }
    else
    {
	return c.putbytes(&head[1], 1); // No length
    }
}

// Send the message header. if 'header' is set then the connection class
// will add the length in when the message is complete.
int dap_message::send_long_header(dap_connection &c)
{
    unsigned char head[] = {6, 0, 0};
    if (c.verbosity() > 2)
        DAPLOG((LOG_INFO, "Sending message of type %s\n", type_name()));

    c.putbytes(&msg_type, 1);
    return c.putbytes(head, 3); // Tell connection to send LEN256 header
}
  

// Read a DAP message header from the connection buffer. Here is where we
// make sure we have the whole message in memory and also that we know
// the message length (used mainly for data messages)
int dap_message::get_header(dap_connection &c)
{
    char *b = c.getbytes(1);
    if (!b) return false;

    flags = *b;

    if (flags & 1) // Got STREAMID (which we just ignore)
    {
	// Throw it away.
	c.getbytes(1);
    }

    if (flags & 2) // got length
    {
	unsigned char l;
	
	c.check_length(1); // Ensure we have the start of a header
	b = c.getbytes(1);
	if (!b) return false;

	l = *b;
	length = l;

	if (flags & 4) // LEN256
	{
	    c.check_length(3); // Ensure we have the len256 bytes
	    
	    b = c.getbytes(1);
	    if (!b) return false;

	    l = *b;
	    length |= l<<8;
	}
	if (length) c.check_length(length);
    }
    else
    {
	length = c.get_length();  // use full block;
	c.check_length(length);   // Marks message end too.
    }

    if (flags & 8) // Got BITCNT(which we just ignore)
    {
	// Throw it away.
	c.getbytes(1);
    }

    if (flags & 16) // SYSPEC is hard.
    {
	fprintf(stderr, "SYSPEC field not supported yet.\n
                         Please email patrick@tykepenguin.cix.co.uk if you REALLY want it.\n");
	exit(999);
    }

    // TODO SYSPEC
    return true;
}

// Peeks at the next nessage in the buffer and returns the
// integer message type. 
// If no message is available then -1 is returned.
int dap_message::peek_message_type(dap_connection& c)
{
    char *b = c.peekbytes(1);
    if (b)
	return *b;
    else
	return -1;    
}

// Read a message from the stream and spit out a fully formed message
dap_message* dap_message::read_message(dap_connection& c, bool block)
{
    unsigned char type;
    char *b;

    dap_message *m = NULL;

    if (!c.read_if_necessary(block))
	return NULL;
    b = c.getbytes(1);

    if (!b) return NULL;

    type = *b;

    switch(type)
    {
    case CONFIG:
	m = new dap_config_message();
	break;
    case ATTRIB:
	m = new dap_attrib_message();
	break;
    case ACCESS:
	m = new dap_access_message();
	break;
    case CONTROL:
	m = new dap_control_message();
	break;
    case CONTRAN:
	m = new dap_contran_message();
	break;
    case ACK:
	m = new dap_ack_message();
	break;
    case ACCOMP:
	m = new dap_accomp_message();
	break;
    case DATA:
	m = new dap_data_message();
	break;
    case STATUS:
	m = new dap_status_message();
	break;
    case DATE:
	m = new dap_date_message();
	break;
    case PROTECT:
	m = new dap_protect_message();
	break;
    case NAME:
	m = new dap_name_message();
	break;
    case ALLOC:
	m = new dap_alloc_message();
	break;
    case SUMMARY:
	m = new dap_summary_message();
	break;
    case KEYDEF:
	m = new dap_key_message();
	break;
    case ACL:
	break; // NYI
    }

    if (c.verbosity() > 2)
	DAPLOG((LOG_INFO, "Got message of type %s\n", type_name(type)));

    // If we got valid message header then read the rest of the message 
    // into the object.
    if (m)
    {
	if (!c.read_if_necessary(true))
	    return NULL;

	if (!m->get_header(c)) return NULL;
	if (!m->read(c)) return NULL;
    }
    else
    {
	DAPLOG((LOG_DEBUG, "Unknown message type 0x%x received\n", type));
	return NULL;
    }
    return m;
}



// Returns the numeric type of a message
unsigned char dap_message::get_type()
{
    return msg_type;
}

char *dap_message::type_name()
{
    return type_name(msg_type);
}

// Return the message type by name
char *dap_message::type_name(int msg_type)
{
    static char name[32];

    switch (msg_type)
    {
    case CONFIG:
	return "CONFIG";
    case ATTRIB:
	return "ATTRIB";
    case ACCESS:
	return "ACCESS";
    case CONTROL:
	return "CONTROL";
    case CONTRAN:
	return "CONTRAN";
    case ACK:
	return "ACK";
    case ACCOMP:
	return "ACCOMP";
    case DATA:
	return "DATA";
    case STATUS:
	return "STATUS";
    case DATE:
	return "DATE";
    case PROTECT:
	return "PROTECT";
    case NAME:
	return "NAME";
    case ALLOC:
	return "ALLOC";
    case SUMMARY:
	return "SUMMARY";
    case KEYDEF:
	return "KEYDEF";
    case ACL:
	return "ACL";
    default:
	sprintf(name, "UNKNOWN (%d)", msg_type);
	return name;
    }
}

//-------------------------- dap_config_message() -----------------------------
#ifdef MIMIC_VMS
bool dap_config_message::write(dap_connection &c)
{
// This is EXACTLY what VMS (5.5) sends

    send_header(c, false); 

    bufsiz.set_short(0x0424);
    bufsiz.write(c);
    ostype.set_byte(0,7);
    ostype.write(c);
    filesys.set_byte(0,3);
    filesys.write(c);
    version.set_byte(0,(unsigned char)7);
    version.set_byte(1,(unsigned char)1);
    version.set_byte(2,(unsigned char)0);
    version.set_byte(3,(unsigned char)5);
    version.set_byte(4,(unsigned char)0);
    version.write(c);
    syscap.set_byte(0,(unsigned char)0xF7);
    syscap.set_byte(1,(unsigned char)0xFb);
    syscap.set_byte(2,(unsigned char)0xd9);
    syscap.set_byte(3,(unsigned char)0xff);
    syscap.set_byte(4,(unsigned char)0xae);
    syscap.set_byte(5,(unsigned char)0xac);
    syscap.set_byte(6,(unsigned char)0x86);
    syscap.set_byte(7,(unsigned char)0x94);
    syscap.set_byte(8,(unsigned char)0xe7);
    syscap.set_byte(9,(unsigned char)0x1b);
    syscap.write(c);
    return c.write();
}
#else
bool dap_config_message::write(dap_connection &c)
{
    send_header(c, false); // Mustn't send length with the CONFIG message

    bufsiz.set_short(buffer_size);
    bufsiz.write(c);
    ostype.set_byte(0,7);  // VAX/VMS (ho ho!)
    ostype.write(c);
    filesys.set_byte(0,3); // RMS-32 (hee hee!)
    filesys.write(c);
    version.set_byte(0,(unsigned char)7);
    version.set_byte(1,(unsigned char)1);
    version.set_byte(2,(unsigned char)0);
    version.set_byte(3,(unsigned char)5);
    version.set_byte(4,(unsigned char)0);
    version.write(c);
    syscap.set_byte(0,(unsigned char)0xA2);
    syscap.set_byte(1,(unsigned char)0xf9); // woz c1
    syscap.set_byte(2,(unsigned char)0xf9);
    syscap.set_byte(3,(unsigned char)0xf4);
    syscap.set_byte(4,(unsigned char)0xAA);
    syscap.set_byte(5,(unsigned char)0x6c);
    syscap.write(c);
    return c.write();
}
#endif

bool dap_config_message::read(dap_connection &c)
{
    if (!bufsiz.read(c))  return false;
    if (!ostype.read(c))  return false;
    if (!filesys.read(c)) return false;
    if (!version.read(c)) return false;
    if (!syscap.read(c))  return false;

    if (!syscap.get_bit(18)) 
    {
	if (c.verbosity()) 
	    DAPLOG((LOG_DEBUG, "Host does not allow blocking\n"));
	c.allow_blocking(false);
    }
    return true;
}

// Return the CRC flag of the config message
bool dap_config_message::need_crc()
{
    return syscap.get_bit(21);
}

//---------------------------- dap_attrib_message() ---------------------------

bool dap_attrib_message::write(dap_connection &c)
{
    send_header(c, true);

    attmenu.write(c);
    if (attmenu.get_bit(0))     datatype.write(c);
    if (attmenu.get_bit(1))     org.write(c);
    if (attmenu.get_bit(2))     rfm.write(c);
    if (attmenu.get_bit(3))     rat.write(c);
    if (attmenu.get_bit(4))     bls.write(c);
    if (attmenu.get_bit(5))     mrs.write(c);
    if (attmenu.get_bit(6))     alq.write(c);
    if (attmenu.get_bit(7))     bks.write(c);
    if (attmenu.get_bit(8))     fsz.write(c);
    if (attmenu.get_bit(9))     mrn.write(c);
    if (attmenu.get_bit(10))    runsys.write(c);
    if (attmenu.get_bit(11))    deq.write(c);
    if (attmenu.get_bit(12))    fop.write(c);
    if (attmenu.get_bit(13))    bsz.write(c);
    if (attmenu.get_bit(14))    dev.write(c);
    if (attmenu.get_bit(15))    sdc.write(c);
    if (attmenu.get_bit(16))    lrl.write(c);
    if (attmenu.get_bit(17))    hbk.write(c);
    if (attmenu.get_bit(18))    ebk.write(c);
    if (attmenu.get_bit(19))    ffb.write(c);
    if (attmenu.get_bit(20))    sbn.write(c);

    return c.write();
}

bool dap_attrib_message::read(dap_connection &c)
{
    attmenu.read(c); 
    if (attmenu.get_bit(0)  &&  !datatype.read(c)) return false;
    if (attmenu.get_bit(1)  &&  !org.read(c)) return false;
    if (attmenu.get_bit(2)  &&  !rfm.read(c)) return false;
    if (attmenu.get_bit(3)  &&  !rat.read(c)) return false;
    if (attmenu.get_bit(4)  &&  !bls.read(c)) return false;
    if (attmenu.get_bit(5)  &&  !mrs.read(c)) return false;
    if (attmenu.get_bit(6)  &&  !alq.read(c)) return false;
    if (attmenu.get_bit(7)  &&  !bks.read(c)) return false;
    if (attmenu.get_bit(8)  &&  !fsz.read(c)) return false;
    if (attmenu.get_bit(9)  &&  !mrn.read(c)) return false;
    if (attmenu.get_bit(10) &&  !runsys.read(c)) return false;
    if (attmenu.get_bit(11) &&  !deq.read(c)) return false;
    if (attmenu.get_bit(12) &&  !fop.read(c)) return false;
    if (attmenu.get_bit(13) &&  !bsz.read(c)) return false;
    if (attmenu.get_bit(14) &&  !dev.read(c)) return false;
    if (attmenu.get_bit(15) &&  !sdc.read(c)) return false;
    if (attmenu.get_bit(16) &&  !lrl.read(c)) return false;
    if (attmenu.get_bit(17) &&  !hbk.read(c)) return false;
    if (attmenu.get_bit(18) &&  !ebk.read(c)) return false;
    if (attmenu.get_bit(19) &&  !ffb.read(c)) return false;
    if (attmenu.get_bit(20) &&  !sbn.read(c)) return false;

    return true;
}

int dap_attrib_message::get_datatype() {return datatype.get_byte(0); }
int dap_attrib_message::get_org(){return org.get_int(); }
int dap_attrib_message::get_rfm(){return rfm.get_int(); }
int dap_attrib_message::get_rat_bit(int v){return rat.get_bit(v); }
int dap_attrib_message::get_rat(){return rat.get_byte(0); }
int dap_attrib_message::get_bls(){return bls.get_int(); }
int dap_attrib_message::get_mrs(){return mrs.get_int(); }
int dap_attrib_message::get_alq(){return alq.get_int(); }
int dap_attrib_message::get_bks(){return bks.get_int(); }
int dap_attrib_message::get_fsz(){return fsz.get_int(); }
int dap_attrib_message::get_mrn(){return mrn.get_int(); }
int dap_attrib_message::get_runsys(){return runsys.get_int(); }
int dap_attrib_message::get_deq(){return deq.get_int(); }
int dap_attrib_message::get_fop_bit(int b){return fop.get_bit(b); }
int dap_attrib_message::get_bsz(){return bsz.get_int(); }
int dap_attrib_message::get_dev_bit(int b){return dev.get_bit(b); }
int dap_attrib_message::get_lrl(){return lrl.get_int(); }
int dap_attrib_message::get_hbk(){return hbk.get_int(); }
int dap_attrib_message::get_ebk(){return ebk.get_int(); }
int dap_attrib_message::get_ffb(){return ffb.get_short(); }
int dap_attrib_message::get_sbn(){return sbn.get_int(); }

// Returns the filesize in bytes
unsigned long dap_attrib_message::get_size()
{
    int i_ebk = ebk.get_int();

    if (i_ebk)
	return ((i_ebk-1)*512 + ffb.get_int());
    else
	return 0;
}

void dap_attrib_message::set_datatype(int v) { datatype.set_byte(0,v); attmenu.set_bit(0); }
void dap_attrib_message::set_org(int v) { org.set_byte(0,v); attmenu.set_bit(1);}
void dap_attrib_message::set_rfm(int v) { rfm.set_byte(0,v); attmenu.set_bit(2);}
void dap_attrib_message::set_rat_bit(int v) { rat.set_bit(v); attmenu.set_bit(3);}
void dap_attrib_message::set_rat(int v) { rat.set_byte(0,v); attmenu.set_bit(3);}
void dap_attrib_message::clear_rat_bit(int v) { rat.clear_bit(v);}
void dap_attrib_message::set_bls(int v) { bls.set_short(v); attmenu.set_bit(4);}
void dap_attrib_message::set_mrs(int v) { mrs.set_short(v); attmenu.set_bit(5);}
void dap_attrib_message::set_alq(int v) { alq.set_int(v); attmenu.set_bit(6);}
void dap_attrib_message::set_bks(int v) { bks.set_byte(0,v); attmenu.set_bit(7);}
void dap_attrib_message::set_fsz(int v) { fsz.set_byte(0,v); attmenu.set_bit(8);}
void dap_attrib_message::set_mrn(int v) { mrn.set_short(v); attmenu.set_bit(9);}
void dap_attrib_message::set_runsys(int v) { runsys.set_short(v); attmenu.set_bit(10);}
void dap_attrib_message::set_deq(int v) { deq.set_short(v); attmenu.set_bit(11);}
void dap_attrib_message::set_fop_bit(int v) { fop.set_bit(v); attmenu.set_bit(12);}
void dap_attrib_message::set_bsz(int v) { bsz.set_byte(0,v); attmenu.set_bit(13);}
void dap_attrib_message::set_dev_bit(int v) { dev.set_bit(v); attmenu.set_bit(14);}
void dap_attrib_message::set_dev_byte(int b, int v) { dev.set_byte(b, v); attmenu.set_bit(14);}
void dap_attrib_message::set_lrl(int v) { lrl.set_short(v); attmenu.set_bit(16);}
void dap_attrib_message::set_hbk(int v) { hbk.set_int(v); attmenu.set_bit(17);}
void dap_attrib_message::set_ebk(int v) { ebk.set_int(v); attmenu.set_bit(18);}
void dap_attrib_message::set_ffb(int v) { ffb.set_short(v); attmenu.set_bit(19);}
void dap_attrib_message::set_sbn(int v) { sbn.set_short(v); attmenu.set_bit(20);}
void dap_attrib_message::remove_dev() {attmenu.clear_bit(14);}

void dap_attrib_message::set_fop(int v) 
{
    fop.set_byte(0, v && 0xFF); 
    fop.set_byte(1, (v>>8) && 0xFF); 
    fop.set_byte(2, (v>>16) && 0xFF); 
    fop.set_byte(3, (v>>24) && 0xFF); 
    attmenu.set_bit(12);
}

// Initialise an attrib message from a file
void dap_attrib_message::set_file(const char *file, bool show_dev)
{
    struct stat st;

    if (stat(file, &st) == 0)
    {
	set_stat(&st, show_dev);
    }
}

// Initialise an attrib message from a stat structure
void dap_attrib_message::set_stat(struct stat *st, bool show_dev)
{
    attmenu.clear_all();

    set_org(FB$SEQ);
    set_rfm(FB$STMLF);
    set_bls(512);
    set_alq(st->st_blocks); // This seems to be 512 byte blocks but I don't know why
    set_ebk((st->st_size+511)/512); // Last block allocated
    set_ffb(st->st_size%512);
    set_bsz(8);
    set_rat(0);
    set_rat_bit(FB$CR); // VMS 7 STM files MUST have carriage control

    // These DEV attributes make VMS request files in BLOCK mode so
    // we only set them when we know we can cope with it.
    if (show_dev)
    {
        set_dev_byte(0, 0x88);
	set_dev_byte(1, 0xCB);
	set_dev_byte(2, 0x4D);
    }

// Set attributes for directories
    if (S_ISDIR(st->st_mode))
    {
	set_rfm(FB$VAR);
	clear_rat_bit(FB$CR);
	set_rat_bit(FB$BLK);
	set_lrl(512);
	set_mrs(512);
	set_fop_bit(FB$CTG);
    }
}



//------------------------- dap_access_message() ------------------------------

bool dap_access_message::write(dap_connection &c)
{
    send_header(c, true);

    accfunc.write(c);
    accopt.write(c);
    filespec.write(c);
    fac.write(c);
    shr.write(c);
    display.write(c);
//    password.write(c);
    return c.write();
}

bool dap_access_message::read(dap_connection &c)
{
    if (!accfunc.read(c)) return false;
    if (!accopt.read(c)) return false;
    if (!filespec.read(c)) return false;
    if (c.have_bytes(1) && !fac.read(c)) return false;
    if (c.have_bytes(1) && !shr.read(c)) return false;
    if (c.have_bytes(1) && !display.read(c)) return false;
    if (c.have_bytes(1) && !password.read(c)) return false;

    return true;
}

void dap_access_message::set_accfunc(int func)
{
    accfunc.set_byte(0,func);
}
void dap_access_message::set_accopt(int opt)
{
    accopt.set_byte(0,opt);
}
void dap_access_message::set_filespec(const char *fn)
{
    filespec.set_string(fn);
}
void dap_access_message::set_fac(int ac)
{
// yes it's bits, but there's only 7 of them
    fac.set_byte(0, ac);
}
void dap_access_message::set_shr(int ac)
{
// yes it's bits, but there's only 7 of them
    shr.set_byte(0,ac);
}
void dap_access_message::set_display(int d)
{
    unsigned char first = d&0x7f;
    unsigned char second = 0;

    // Just two more bits to set
    if (d & 0x080) second  = 1;
    if (d & 0x100) second |= 2;

    if (second) first |= 0x80;

    display.set_byte(0, first);
    display.set_byte(1, second);
}


int dap_access_message::get_accfunc()
{
    return accfunc.get_byte(0);
}
char *dap_access_message::get_filespec()
{
    return filespec.get_string();
}
int dap_access_message::get_accopt()
{
    return accopt.get_byte(0);
}
int dap_access_message::get_fac()
{
    return fac.get_byte(0);
}
int dap_access_message::get_shr()
{
    return shr.get_byte(0);
}
int dap_access_message::get_display()
{
    int d;
    d = display.get_byte(0);
    if (d&0x80)
    {
	d &= 0x7f;
	d |= display.get_byte(1)<<7;
    }

    return d;
}

//------------------------ dap_control_message() ------------------------------

bool dap_control_message::write(dap_connection &c)
{
    send_header(c, true);
    ctlfunc.write(c);
    ctlmenu.write(c);
    if (ctlmenu.get_bit(0))  rac.write(c);
    if (ctlmenu.get_bit(1))  key.write(c);
    if (ctlmenu.get_bit(2))  krf.write(c);
    if (ctlmenu.get_bit(3))  rop.write(c);
    if (ctlmenu.get_bit(4))  hsh.write(c);
    if (ctlmenu.get_bit(5))  display.write(c);
    return c.write();
}

bool dap_control_message::read(dap_connection &c)
{
    if (!ctlfunc.read(c)) return false;
    if (!ctlmenu.read(c)) return false;
    if (ctlmenu.get_bit(0) &&  !rac.read(c)) return false;
    if (ctlmenu.get_bit(1) &&  !key.read(c)) return false;
    if (ctlmenu.get_bit(2) &&  !krf.read(c)) return false;
    if (ctlmenu.get_bit(3) &&  !rop.read(c)) return false;
    if (ctlmenu.get_bit(4) &&  !hsh.read(c)) return false;
    if (ctlmenu.get_bit(5) &&  !display.read(c)) return false;
    return true;
}
void dap_control_message::set_ctlfunc(int f)
{
    ctlfunc.set_byte(0,f);
}

void dap_control_message::set_rac(int f)
{
    rac.set_byte(0,f);
    ctlmenu.set_bit(0);
}

void dap_control_message::set_key(const char *k)
{
    key.set_string(k);
    ctlmenu.set_bit(1);
}

void dap_control_message::set_krf(int f)
{
    krf.set_byte(0,f);
    ctlmenu.set_bit(2);
}

void dap_control_message::set_rop_bit(int f)
{
    rop.set_bit(f);
    ctlmenu.set_bit(3);
}

void dap_control_message::set_rop(int f)
{
    rop.set_byte(0,f);
    ctlmenu.set_bit(3);
}


int dap_control_message::get_ctlfunc()
{
    return ctlfunc.get_byte(0);
}

int dap_control_message::get_rac()
{
    return rac.get_byte(0);
}

char *dap_control_message::get_key()
{
    return key.get_string();
}

unsigned long dap_control_message::get_long_key()
{
    return key.get_int();
}

int dap_control_message::get_krf()
{
    return krf.get_byte(0);
}

bool dap_control_message::get_rop_bit(int f)
{
    return rop.get_bit(f);
}

int dap_control_message::get_display()
{
    int d;
    d = display.get_byte(0);
    if (d&0x80)
    {
	d &= 0x7f;
	d |= display.get_byte(1)<<7;
    }

    return d;
}

void dap_control_message::set_display(int d)
{
    unsigned char first = d&0x7f;
    unsigned char second = 0;

    // Just two more bits to set
    if (d & 0x080) second  = 1;
    if (d & 0x100) second |= 2;

    if (second) first |= 0x80;

    display.set_byte(0, first);
    display.set_byte(1, second);

    ctlmenu.set_bit(5);
}

//------------------------ dap_contran_message() ------------------------------

bool dap_contran_message::write(dap_connection &c)
{
    send_header(c, false);

    confunc.write(c);
    return c.write();
}

bool dap_contran_message::read(dap_connection &c)
{
    if (!confunc.read(c)) return false;
    return true;
}

int dap_contran_message::get_confunc()
{
    return confunc.get_byte(0);
}

void dap_contran_message::set_confunc(int f)
{
    confunc.set_byte(0,f);
}

//--------------------------- dap_ack_message() ------------------------------

bool dap_ack_message::write(dap_connection &c)
{
    send_header(c, true);

    return c.write();
}

bool dap_ack_message::read(dap_connection &c)
{
    return true;
}

//------------------------- dap_accomp_message() ------------------------------

bool dap_accomp_message::write(dap_connection &c)
{
    send_header(c, true);

    cmpfunc.write(c);
    if (send_fop) fop.write(c);
//    check.write(c);
    return c.write();
}

bool dap_accomp_message::read(dap_connection &c)
{
    if (!cmpfunc.read(c)) return false;
    if (c.have_bytes(1) && !fop.read(c)) return false;   // Optional
    if (c.have_bytes(1) && !check.read(c)) return false; // Optional
    return true;
}

int dap_accomp_message::get_cmpfunc()
{
    return cmpfunc.get_byte(0);
}

int dap_accomp_message::get_fop_bit(int b)
{
    return fop.get_bit(b);
}

int dap_accomp_message::get_check()
{
    return cmpfunc.get_int();
}

void dap_accomp_message::set_cmpfunc(int f)
{
    cmpfunc.set_byte(0,f);
}

void dap_accomp_message::set_fop_bit(int bit)
{
    send_fop = true;
    fop.set_bit(bit);
}

void dap_accomp_message::set_check(int c)
{
    cmpfunc.set_int(c);
}

//---------------------------- dap_data_message() -----------------------------

dap_data_message::~dap_data_message()
{
    if (local_data) delete[] data;
}

bool dap_data_message::write(dap_connection &c)
{
    send_header(c, false);// Never send a length count

    recnum.write(c);
    c.putbytes(data, length);    
    return c.write();
}

bool dap_data_message::write_with_len(dap_connection &c)
{
    if (length+recnum.get_length() >= 255)
	send_long_header(c);
    else
	send_header(c, true);

    recnum.write(c);
    c.putbytes(data, length);    
    return c.write();
}

// Only call this if you know what you're doing. (ie you know EXACTLY
// the length of the recnum field)
bool dap_data_message::write_with_len256(dap_connection &c)
{
    send_long_header(c);

    recnum.write(c);
    c.putbytes(data, length);
    return c.write();
}
 
bool dap_data_message::read(dap_connection &c)
{
    if (!recnum.read(c)) return false;

    // The length  of the message includes the recnum field
    length -=  recnum.get_length()+1;
    char *b = c.getbytes(length);
    if (!b) return false;

    // Just keep a pointer to the transfer buffer for speed
    data = b;
    local_data = false;
    return true;
}

int dap_data_message::get_datalen()
{
    return length;
}

// Horribly dangerous but beautifully fast.
char *dap_data_message::get_dataptr()
{
    return data;
}

void dap_data_message::get_data(char *d, int *len)
{
    *len = length;
    memcpy(d, data, length);
}

void dap_data_message::set_data(const char *d, int len)
{
    if (data && local_data) delete[] data;
    data = new char[len];
    memcpy(data, d, len);
    length = len;
    local_data = true;
}

int dap_data_message::get_recnum()
{
    return recnum.get_short();
}

void dap_data_message::set_recnum(int r)
{
    recnum.set_short(r);
}

//------------------------- dap_status_message() ------------------------------

bool dap_status_message::write(dap_connection &c)
{
    send_header(c, true);

    stscode.write(c);
// TODO: which of these we send depends on all sorts of things (including
// TODO: the ACCESS message)
    
    if (rfa.get_int()) rfa.write(c);
//    recnum.write(c);
//    stv.write(c);
    return c.write();
}

bool dap_status_message::read(dap_connection &c)
{
    if (!stscode.read(c)) return false;
    if (c.have_bytes(1) && !rfa.read(c)) return false;
    if (c.have_bytes(1) && !recnum.read(c)) return false;
    if (c.have_bytes(1) && !stv.read(c)) return false;
    return true;
}

int dap_status_message::get_code()
{
    return stscode.get_short();
}

void dap_status_message::set_code(int s)
{
    stscode.set_short(s);
}

long dap_status_message::get_rfa()
{
    return rfa.get_short();
}

void dap_status_message::set_rfa(long r)
{
    rfa.set_int(r);
}

void dap_status_message::set_errno()
{
    // 0x4000 says this is an Open error code (which most of ours are)
    stscode.set_short(0x4000 | errno_to_stscode());
}

void dap_status_message::set_errno(int er)
{
    errno = er;
    stscode.set_short(0x4000 | errno_to_stscode());
}
 
// Convert errno to a DAP status message:
int dap_status_message::errno_to_stscode()
{
    switch (errno) // Loads missing here!!
    {
    case 0:         return 0225;
    case EPERM:     return 0125;
    case ENOENT:    return 062;
    case EINTR:     return 011;
    case EIO:       return 02;
    case ENXIO:     return 035;
    case ENOMEM:    return 037;
    case EACCES:    return 0125;
    case EBUSY:     return 041;
    case EEXIST:    return 055;
    case ENODEV:    return 035;
    case ENOTDIR:   return 040;
    case EINVAL:    return 0203;
    case ENFILE:
    case EMFILE:    return 0307;
    case ETXTBSY:   return 060;
    case EFBIG:     return 0301;
    case EROFS:     return 0164;
    case ENOTCONN:  return 01;
    case ENOSPC:    return 065;
    default:
	DAPLOG((LOG_DEBUG, "no DAP error for %d - sending default\n", errno));
	return 01;
    }
}

char *dap_status_message::get_message()
{
    switch (stscode.get_int() & 0xFF)
    {
    case 01: return "operation aborted.";
    case 02: return "F11-ACP could not access file.";
    case 03: return "\"FILE\" activity precludes operation.";
    case 04: return "bad area ID.";
    case 05: return "alignment options error.";
    case 06: return "allocation quantity too large or 0 value.";
    case 07: return "not ANSI \"D\" format.";
    case 010: return "allocation options error.";
    case 011: return "invalid (i.e., synch) operation at AST level.";
    case 012: return "attribute read error.";
    case 013: return "attribute write error.";
    case 014: return "bucket size too large.";
    case 015: return "bucket size too large.";
    case 016: return "\"BLN\" length error.";
    case 017: return "beginning of file detected.";
    case 020: return "private pool address.";
    case 021: return "private pool size.";
    case 022: return "internal RMS error condition detected.";
    case 023: return "cannot connect RAB.";
    case 024: return "$UPDATE changed a key without having attribute of XB$CHG set.";
    case 025: return "bucket format check-byte failure.";
    case 026: return "RSTS/E close function failed.";
    case 027: return "invalid or unsupported \"COD\" field.";
    case 030: return "F11-ACP could not create file (STV=sys err code).";
    case 031: return "no current  record  (operation  not  preceded  by GET/FIND).";
    case 032: return "F11-ACP deaccess error during \"CLOSE\".";
    case 033: return "data \"AREA\" number invalid.";
    case 034: return "RFA-Accessed record was deleted.";
    case 035: return "bad device, or inappropriate device type.";
    case 036: return "error in directory name.";
    case 037: return "dynamic memory exhausted.";
    case 040: return "directory not found.";
    case 041: return "device not ready.";
    case 042: return "device has positioning error.";
    case 043: return "\"DTP\" field invalid.";
    case 044: return "duplicate key detected, XB$DUP not set.";
    case 045: return "RSX-F11ACP enter function failed.";
    case 046: return "operation not selected in \"ORG$\" macro.";
    case 047: return "end-of-file.";
    case 050: return "expanded string area too short.";
    case 051: return "file expiration date not yet reached.";
    case 052: return "file extend failure.";
    case 053: return "not a valid FAB (\"BID\" NOT = FB$BID).";
    case 054: return "illegal FAC for REC-OP,0, or FB$PUT not  set  for \"CREATE\".";
    case 055: return "file already exists.";
    case 056: return "invalid file I.D.";
    case 057: return "invalid flag-bits combination.";
    case 060: return "file is locked by other user.";
    case 061: return "RSX-F11ACP \"FIND\" function failed.";
    case 062: return "file not found.";
    case 063: return "error in file name.";
    case 064: return "invalid file options.";
    case 065: return "DEVICE/FILE full.";
    case 066: return "index \"AREA\" number invalid.";
    case 067: return "invalid IFI value or unopened file.";
    case 070: return "maximum NUM(254) areas/key XABS exceeded.";
    case 071: return "$INIT macro never issued.";
    case 072: return "operation illegal or invalid for file organization.";
    case 073: return "illegal record encountered (with sequential files only).";
    case 074: return "invalid ISI value, on unconnected RAB.";
    case 075: return "bad KEY buffer address (KBF=0).";
    case 076: return "invalid KEY field (KEY=0/neg).";
    case 077: return "invalid key-of-reference ($GET/$FIND).";
    case 0100: return "KEY size too large.";
    case 0101: return "lowest-level-index \"AREA\" number invalid.";
    case 0102: return "not ANSI labeled tape.";
    case 0103: return "logical channel busy.";
    case 0104: return "logical channel number too large.";
    case 0105: return "logical extend error, prior extend still valid.";
    case 0106: return "\"LOC\" field invalid.";
    case 0107: return "buffer mapping error.";
    case 0110: return "F11-ACP could not mark file for deletion.";
    case 0111: return "MRN value=neg or relative key>MRN.";
    case 0112: return "MRS value=0 for fixed length records.  Also 0 for relative files.";
    case 0113: return "\"NAM\"  block  address  invalid  (NAM=0,  or   not accessible).";
    case 0114: return "not positioned to EOF (sequential files only).";
    case 0115: return "cannot allocate internal index descriptor.";
    case 0116: return "indexed file; no primary key defined.";
    case 0117: return "RSTS/E open function failed.";
    case 0120: return "XAB'S not in correct order.";
    case 0121: return "invalid file organization value.";
    case 0122: return "error in file's prologue (reconstruct file).";
    case 0123: return "\"POS\" field invalid (POS>MRS,STV=XAB indicator).";
    case 0124: return "bad file date field retrieved.";
    case 0125: return "privilege violation (OS denies access).";
    case 0126: return "not a valid RAB (\"BID\" NOT=RB$BID).";
    case 0127: return "illegal RAC value.";
    case 0130: return "illegal record attributes.";
    case 0131: return "invalid record  buffer  address  (\"ODD\",  or  not word-aligned if BLK-IO).";
    case 0132: return "file read error.";
    case 0133: return "record already exists.";
    case 0134: return "bad RFA value (RFA=0).";
    case 0135: return "invalid record format.";
    case 0136: return "target bucket locked by another stream.";
    case 0137: return "RSX-F11 ACP remove function failed.";
    case 0140: return "record not found.";
    case 0141: return "record not locked.";

    case 0143: return "error while reading prologue.";
    case 0144: return "invalid RRV record encountered.";
    case 0145: return "RAB stream currently active.";
    case 0146: return "bad record size (RSZ>MRS,  or  NOT=MRS  if  fixed length records).";
    case 0147: return "record too big for user's buffer.";
    case 0150: return "primary  key  out  of  sequence  (RAC=RB$SEQ  for $PUT).";
    case 0151: return "\"SHR\"  field  invalid  for  file  (cannot   share sequential files).";
    case 0152: return "\"SIZ field invalid.";
    case 0153: return "stack too big for save area.";
    case 0154: return "system directive error.";
    case 0155: return "index tree error.";
    case 0156: return "error in file type extension on FNS too big.";
    case 0157: return "invalid user buffer addr (0, odd, or if  BLK-IO not word aligned).";
    case 0160: return "invalid user buffer size (USZ=0).";
    case 0161: return "error in version number.";
    case 0162: return "invalid volume number.";
    case 0163: return "file write error (STV=sys err code).";
    case 0164: return "device is write locked.";
    case 0165: return "error while writing prologue.";
    case 0166: return "not a valid XAB (@XAB=ODD,STV=XAB indicator).";
    case 0167: return "default directory invalid.";
    case 0170: return "cannot access argument list.";
    case 0171: return "cannot close file.";
    case 0172: return "cannot deliver AST.";
    case 0173: return "channel assignment failure (STV=sys err code).";
    case 0174: return "terminal output ignored due to (CNTRL) O.";
    case 0175: return "terminal input aborted due to (CNTRL) Y.";
    case 0176: return "default filename string address error.";
    case 0177: return "invalid device I.D.  field.";
    case 0200: return "expanded string address error.";
    case 0201: return "filename string address error.";
    case 0202: return "FSZ field invalid.";
    case 0203: return "invalid argument list.";
    case 0204: return "known file found.";
    case 0205: return "logical name error.";
    case 0206: return "node name error.";
    case 0207: return "operation successful.";
    case 0210: return "record inserted had duplicate key.";
    case 0211: return "index update error occurred-record inserted.";
    case 0212: return "record locked but read anyway.";
    case 0213: return "record  inserted  in  primary  o.k.; may  not  be accessible by secondary keys or RFA.";
    case 0214: return "file was created, but not opened.";
    case 0215: return "bad prompt buffer address.";
    case 0216: return "async.  operation pending completion.";
    case 0217: return "quoted string error.";
    case 0220: return "record header buffer invalid.";
    case 0221: return "invalid related file.";
    case 0222: return "invalid resultant string size.";
    case 0223: return "invalid resultant string address.";
    case 0224: return "operation not sequential.";
    case 0225: return "operation successful.";
    case 0226: return "created file superseded existing version.";
    case 0227: return "filename syntax error.";
    case 0230: return "time-out period expired.";
    case 0231: return "FB$BLK record attribute not supported.";
    case 0232: return "bad byte size.";
    case 0233: return "cannot disconnect RAB.";
    case 0234: return "cannot get JFN for file.";
    case 0235: return "cannot open file.";
    case 0236: return "bad JFN value.";
    case 0237: return "cannot position to end-of-file.";
    case 0240: return "cannot truncate file.";
    case 0241: return "file is currently in an  undefined  state; access is denied.";
    case 0242: return "file must be opened for exclusive access.";
    case 0243: return "directory full.";
    case 0244: return "handler not in system.";
    case 0245: return "fatal hardware error.";
    case 0246: return "attempt to write beyond EOF.";
    case 0247: return "hardware option not present.";
    case 0250: return "device not attached.";
    case 0251: return "device already attached.";
    case 0252: return "device not attachable.";
    case 0253: return "sharable resource in use.";
    case 0254: return "illegal overlay request.";
    case 0255: return "block check or CRC error.";
    case 0256: return "caller's nodes exhausted.";
    case 0257: return "index file full.";
    case 0260: return "file header full.";
    case 0261: return "accessed for write.";
    case 0262: return "file header checksum failure.";
    case 0263: return "attribute control list error.";
    case 0264: return "file already accessed on LUN.";
    case 0265: return "bad tape format.";
    case 0266: return "illegal operation on file descriptor block.";
    case 0267: return "rename; 2 different devices.";
    case 0270: return "rename; new filename already in use.";
    case 0271: return "cannot rename old file system.";
    case 0272: return "file already open.";
    case 0273: return "parity error on device.";
    case 0274: return "end of volume detected.";
    case 0275: return "data over-run.";
    case 0276: return "bad block on device.";
    case 0277: return "end of tape detected.";
    case 0300: return "no buffer space for file.";
    case 0301: return "file exceeds allocated space -- no blks.";
    case 0302: return "specified task not installed.";
    case 0303: return "unlock error.";
    case 0304: return "no file accessed on LUN.";
    case 0305: return "send/receive failure.";
    case 0306: return "spool or submit command file failure.";
    case 0307: return "no more files.";
    case 0310: return "DAP file transfer Checksum error.";
    case 0311: return "Quota exceeded";
    case 0312: return "internal network error condition detected.";
    case 0313: return "terminal input aborted due to (CNTRL) C.";
    case 0314: return "data bucket fill size > bucket size in XAB.";
    case 0315: return "invalid expanded string length.";
    case 0316: return "illegal bucket format.";
    case 0317: return "bucket size of LAN NOT = IAN in XAB.";
    case 0320: return "index not initialized.";
    case 0321: return "illegal file attributes (corrupt file header).";
    case 0322: return "index bucket fill size > bucket size in XAB.";
    case 0323: return "key name buffer not readable or writeable in XAB.";
    case 0324: return "index bucket will not hold two keys  for  key  of reference.";
    case 0325: return "multi-buffer count invalid (negative value).";
    case 0326: return "network operation failed at remote node.";
    case 0327: return "record is already locked.";
    case 0330: return "deleted record successfully accessed.";
    case 0331: return "retrieved record exceeds specified key value.";
    case 0332: return "key XAB not filled in.";
    case 0333: return "nonexistent record successfully accessed.";
    case 0334: return "unsupported prologue version.";
    case 0335: return "illegal key-of-reference in XAB.";
    case 0336: return "invalid resultant string length.";
    case 0337: return "error updating rrv's, some paths to data  may  be lost.";
    case 0340: return "data types  other  than  string  limited  to  one segment in XAB.";
    case 0341: return "reserved";
    case 0342: return "operation not supported over network.";
    case 0343: return "error on write behind.";
    case 0344: return "invalid wildcard operation.";
    case 0345: return "working set full (can not lock buffers in working set.)";
    case 0346: return "directory listing -- error in reading  volume-set name, directory name, of file name.";
    case 0347: return "directory  listing  --  error  in  reading   file attributes.";
    case 0350: return "directory  listing  --  protection  violation  in trying  to read the volume-set, directory or file name.";
    case 0351: return "directory  listing  --  protection  violation  in trying to read file attributes.";
    case 0352: return "directory  listing  --  file  attributes  do  not exist.";
    case 0353: return "directory listing -- unable to recover  directory list after Continue Transfer (Skip).";
    case 0354: return "sharing not enabled.";
    case 0355: return "sharing page count exceeded.";
    case 0356: return "UPI bit not set when sharing with BRO set.";
    case 0357: return "error in access control string (poor man's  route through error).";
    case 0360: return "terminator not seen.";
    case 0361: return "bad escape sequence.";
    case 0362: return "partial escape sequence.";
    case 0363: return "invalid wildcard context value.";
    case 0364: return "invalid directory rename operation.";
    case 0365: return "user structure (FAB/RAB)  became  invalid  during operation.";
    case 0366: return "network file transfer made precludes operation.";

    default:
	return "unknown error";
    }
}
//------------------------------ dap_name_message() ---------------------------

bool dap_name_message::write(dap_connection &c)
{
    send_header(c, true);

    nametype.write(c);
    namespec.write(c);
    return c.write();
}

bool dap_name_message::read(dap_connection &c)
{
    if (!nametype.read(c)) return false;
    if (!namespec.read(c)) return false;
    return true;
}

int dap_name_message::get_nametype()
{
    return nametype.get_byte(0);
}

char *dap_name_message::get_namespec()
{
    return namespec.get_string();
}


void dap_name_message::set_nametype(int t)
{
    nametype.set_byte(0, t);
}

void dap_name_message::set_namespec(const char *n)
{
    namespec.set_string(n);
}

//------------------------------- dap_date_message() --------------------------
bool dap_date_message::write(dap_connection &c)
{
    send_header(c, true);

    datmenu.write(c);
    if (datmenu.get_bit(0)) cdt.write(c);
    if (datmenu.get_bit(1)) rdt.write(c);
    if (datmenu.get_bit(2)) edt.write(c);
    if (datmenu.get_bit(3)) rvn.write(c);
    if (datmenu.get_bit(4)) bdt.write(c);
    return c.write();
}

bool dap_date_message::read(dap_connection &c)
{
    datmenu.read(c);
    if (datmenu.get_bit(0) && !cdt.read(c)) return false;
    if (datmenu.get_bit(1) && !rdt.read(c)) return false;
    if (datmenu.get_bit(2) && !edt.read(c)) return false;
    if (datmenu.get_bit(3) && !rvn.read(c)) return false;
    if (datmenu.get_bit(4) && !bdt.read(c)) return false;
    return true;
}

char *dap_date_message::get_cdt()
{
    return cdt.get_string();
}

char *dap_date_message::get_rdt()
{
    return rdt.get_string();
}

char *dap_date_message::get_edt()
{
    return edt.get_string();
}

char *dap_date_message::get_bdt()
{
    return bdt.get_string();
}

time_t dap_date_message::get_cdt_time()
{
    return string_to_time_t(edt.get_string());
}

time_t dap_date_message::get_rdt_time()
{
    return string_to_time_t(edt.get_string());
}

time_t dap_date_message::get_edt_time()
{
    return string_to_time_t(edt.get_string());
}


time_t dap_date_message::get_bdt_time()
{
    return string_to_time_t(bdt.get_string());
}

int dap_date_message::get_rvn()
{
    return rvn.get_short();
}

void dap_date_message::set_cdt(time_t t)
{
    cdt.set_string(time_to_string(t));
    datmenu.set_bit(0);
}

void dap_date_message::set_rdt(time_t t)
{
    rdt.set_string(time_to_string(t));
    datmenu.set_bit(1);
}

void dap_date_message::set_edt(time_t t)
{
    edt.set_string(time_to_string(t));
    datmenu.set_bit(2);
}
void dap_date_message::set_bdt(time_t t)
{
    bdt.set_string(time_to_string(t));
    datmenu.set_bit(4);
}

void dap_date_message::set_rvn(int r)
{
    rvn.set_short(r);
    datmenu.set_bit(3);
}

char *dap_date_message::time_to_string(time_t t)
{
    static char d[25];
    struct tm tm = *localtime(&t);

    strftime(d, sizeof(d), "%d-%b-%y %H:%M:%S", &tm);

// Convert the month to uppercase (strftime makes the last two letters lower)
    d[4] = toupper(d[4]);
    d[5] = toupper(d[5]);
    
    return d;
}

time_t dap_date_message::string_to_time_t(const char *d)
{
    struct tm tm;
    char month[5];

    sscanf(d, "%d-%s-%d %d:%d:%d", 
	   &tm.tm_mday, month,      &tm.tm_year, 
	   &tm.tm_hour, &tm.tm_min, &tm.tm_sec);

    tm.tm_isdst = -1;

    int i = 0;
    while (i < NUM_MONTHS)
    {
	if (!strcmp(months[i++], month))
	{
	    tm.tm_mon = i;
	    break;
	}
    }
    return mktime(&tm);
}

// Make a two-digit date into a 4-digit one. using 1970 as the pivot date
char *dap_date_message::make_y2k(char *dt)
{
    static char y2kdate[25];
    int year;

    strcpy(y2kdate, dt);
    
    year = (dt[7]-'0')*10 + dt[8]-'0';
    if (year >= 70) 
	year += 1900;
    else
	year += 2000;

    char yearstr[5];
    sprintf(yearstr, "%04d", year);
    y2kdate[7] = '\0';
    strcat(y2kdate, yearstr);
    strcat(y2kdate, dt+9);

    return y2kdate;
}

char *dap_date_message::months[]={"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
				  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

//------------------------- dap_alloc_message() ------------------------------

bool dap_alloc_message::read(dap_connection &c)
{
    allmenu.read(c);
    if (allmenu.get_bit(0) && !vol.read(c)) return false;
    if (allmenu.get_bit(1) && !aln.read(c)) return false;
    if (allmenu.get_bit(2) && !aop.read(c)) return false;
    if (allmenu.get_bit(3) && !loc.read(c)) return false;
    if (allmenu.get_bit(4) && !rfi.read(c)) return false;
    if (allmenu.get_bit(5) && !alq.read(c)) return false;
    if (allmenu.get_bit(6) && !aid.read(c)) return false;
    if (allmenu.get_bit(7) && !bkz.read(c)) return false;
    if (allmenu.get_bit(8) && !deq.read(c)) return false;
    return true;
}

bool dap_alloc_message::write(dap_connection &c)
{
    send_header(c, true);

    allmenu.write(c);
    if (allmenu.get_bit(0)) vol.write(c);
    if (allmenu.get_bit(1)) aln.write(c);
    if (allmenu.get_bit(2)) aop.write(c);
    if (allmenu.get_bit(3)) loc.write(c);
    if (allmenu.get_bit(4)) rfi.write(c);
    if (allmenu.get_bit(5)) alq.write(c);
    if (allmenu.get_bit(6)) aid.write(c);
    if (allmenu.get_bit(7)) bkz.write(c);
    if (allmenu.get_bit(8)) deq.write(c);
    return c.write();
}

//------------------------ dap_protect_message() ------------------------------

bool dap_protect_message::read(dap_connection &c)
{
    protmenu.read(c);
    if (protmenu.get_bit(0) && !owner.read(c))   return false;
    if (protmenu.get_bit(1) && !protsys.read(c)) return false;
    if (protmenu.get_bit(2) && !protown.read(c)) return false;
    if (protmenu.get_bit(3) && !protgrp.read(c)) return false;
    if (protmenu.get_bit(4) && !protwld.read(c)) return false;
    return true;
}

bool dap_protect_message::write(dap_connection &c)
{
    send_header(c, true);

    protmenu.write(c);
    if (protmenu.get_bit(0)) owner.write(c);
    if (protmenu.get_bit(1)) protsys.write(c);
    if (protmenu.get_bit(2)) protown.write(c);
    if (protmenu.get_bit(3)) protgrp.write(c);
    if (protmenu.get_bit(4)) protwld.write(c);
    return c.write();
}

mode_t dap_protect_message::get_mode()
{
    mode_t mode = 0;
    int p = 0;

// Is it present?
    if (!protmenu.get_bit(1)) return 0;

    for (int i=0; i<4; i++)
    {
	switch (i)
	{
	case 0:
	    continue; // No Unix equivalent.
	case 1:
	    p = protown.get_byte(0);
	    break;
	case 2:
	    p = protgrp.get_byte(0);
	    break;
	case 3:
	    p = protwld.get_byte(0);
	    break;
	}
	mode = mode << 3;
	if (!(p & 0x01)) mode |= 4;
	if (!(p & 0x02)) mode |= 2;
	if (!(p & 0x04)) mode |= 1;
    }
    
    return mode;
}

char *dap_protect_message::get_protection()
{
    static char protstring[60];
    int p = 0;
    int ptr = 0;
    int i;

// Is it present?
    if (!protmenu.get_bit(1)) return "";

    protstring[ptr++] = '(';
    for (i=0; i<4; i++)
    {
	switch (i)
	{
	case 0:
	    p = protsys.get_byte(0);
	    break;
	case 1:
	    p = protown.get_byte(0);
	    break;
	case 2:
	    p = protgrp.get_byte(0);
	    break;
	case 3:
	    p = protwld.get_byte(0);
	    break;
	}

	if (!(p & 0x01)) protstring[ptr++] = 'R';
	if (!(p & 0x02)) protstring[ptr++] = 'W';
	if (!(p & 0x04)) protstring[ptr++] = 'E';
	if (!(p & 0x08)) protstring[ptr++] = 'D';
	if (i != 3)   protstring[ptr++] = ',';
    }
    protstring[ptr++] = ')';
    protstring[ptr++] = '\0';
    return protstring;
}

char *dap_protect_message::get_owner()
{
    return owner.get_string();
}

void dap_protect_message::set_owner(const char *o)
{
    owner.set_string(o);
    protmenu.set_bit(0);
}

void dap_protect_message::set_owner(gid_t g, uid_t o)
{
#if 0
    // VMS (5.5) seems not to like the character version
    struct passwd *pw = getpwuid(o);
    struct group  *gr = getgrgid(g);

/* If there's no username for this UID then send the number */
    if (pw && gr) 
    {
        char ownuid[32];
	sprintf(ownuid, "[%s,%s]",gr->gr_name, pw->pw_name);
	dap_connection::makeupper(ownuid);
        owner.set_string(ownuid);
    }
    else
#endif
    {
        char ownuid[32];
        sprintf(ownuid, "[%o,%o]", g,o);
        owner.set_string(ownuid);
    }
    protmenu.set_bit(0);
}

unsigned char dap_protect_message::unix_to_vms(unsigned int prot)
{
   unsigned char bits = 0;

   // DAP specifies that bit ON denies access.
   if ((prot & 1) == 0) bits |= 0x04;  // execute
   if ((prot & 2) == 0) bits |= 0x0a;  // write & delete
   if ((prot & 4) == 0) bits |= 0x01;  // read

   return bits;

}

void dap_protect_message::set_protection(mode_t mode)
{
    protsys.set_byte(0, 0x00); // S:RWED
    protown.set_byte(0, unix_to_vms( (mode>>6) & 0x07));
    protgrp.set_byte(0, unix_to_vms( (mode>>3) & 0x07));
    protwld.set_byte(0, unix_to_vms( mode & 0x07));

    protmenu.set_bit(1);
    protmenu.set_bit(2);
    protmenu.set_bit(3);
    protmenu.set_bit(4);

}

//------------------------ dap_summary_message() ------------------------------

bool dap_summary_message::read(dap_connection &c)
{
    summenu.read(c);
    if (summenu.get_bit(0) && !nok.read(c)) return false;
    if (summenu.get_bit(1) && !noa.read(c)) return false;
    if (summenu.get_bit(2) && !nor.read(c)) return false;
    if (summenu.get_bit(3) && !pvn.read(c)) return false;
    return true;
}

bool dap_summary_message::write(dap_connection &c)
{
    send_header(c, true);

    summenu.write(c);
    if (summenu.get_bit(0)) nok.write(c);
    if (summenu.get_bit(1)) noa.write(c);
    if (summenu.get_bit(2)) nor.write(c);
    if (summenu.get_bit(3)) pvn.write(c);
    return c.write();
}

int dap_summary_message::get_nok()
{
    return nok.get_int();
}
int dap_summary_message::get_noa()
{
    return noa.get_int();
}
int dap_summary_message::get_nor()
{
    return nor.get_int();
}
int dap_summary_message::get_pvn()
{
    return pvn.get_int();
}
//------------------------ dap_key_message() ------------------------------

// A very limited key message. enough to solicit a full one from VMS
bool dap_key_message::write(dap_connection &c)
{
    send_header(c, true);

    keymenu.write(c);
    if (keymenu.get_bit(4)) ref.write(c);
    if (keymenu.get_bit(5)) knm.write(c);
    return c.write();
}

dap_key_message::~dap_key_message()
{
    if (keymenu.get_bit(3))
    {
	int segs = nsg.get_int();

	for (int i=0; i<segs; i++)
	{
	    delete pos[i];
	    delete siz[i];
	}
	delete[] pos;
	delete[] siz;
    }
}

bool dap_key_message::read(dap_connection &c)
{
    keymenu.read(c);
    if (keymenu.get_bit(0) && !flg.read(c)) return false;
    if (keymenu.get_bit(1) && !dfl.read(c)) return false;
    if (keymenu.get_bit(2) && !ifl.read(c)) return false;
    if (keymenu.get_bit(3) && !nsg.read(c)) return false;
    if (keymenu.get_bit(3))
    {
	int segs = nsg.get_int();

	pos = new dap_bytes*[segs];
	siz = new dap_bytes*[segs];
	for (int i=0; i<segs; i++)
	{
	    pos[i] = new dap_bytes(1);
	    siz[i] = new dap_bytes(1);
	    pos[i]->read(c);
	    siz[i]->read(c);

	    fprintf(stderr, "key %d, pos= %d, size=%d\n",
		    i, pos[i]->get_int(),siz[i]->get_int());
	}
    }
    if (keymenu.get_bit(4) && !ref.read(c)) return false;
    if (keymenu.get_bit(5) && !knm.read(c)) return false;
    if (keymenu.get_bit(6) && !nul.read(c)) return false;
    if (keymenu.get_bit(7) && !ian.read(c)) return false;
    if (keymenu.get_bit(8) && !lan.read(c)) return false;
    if (keymenu.get_bit(9) && !dan.read(c)) return false;
    if (keymenu.get_bit(10) && !dtp.read(c)) return false;
    if (keymenu.get_bit(11) && !rvb.read(c)) return false;
    if (keymenu.get_bit(12) && !hal.read(c)) return false;
    if (keymenu.get_bit(13) && !dvb.read(c)) return false;
    if (keymenu.get_bit(14) && !dbs.read(c)) return false;
    if (keymenu.get_bit(15) && !ibs.read(c)) return false;
    if (keymenu.get_bit(16) && !lvl.read(c)) return false;
    if (keymenu.get_bit(17) && !tks.read(c)) return false;
    if (keymenu.get_bit(18) && !mrl.read(c)) return false;

    return true;
}

char *dap_key_message::get_name()
{
    return knm.get_string();
}

int dap_key_message::get_flag()
{
    return flg.get_byte(1);
}

int dap_key_message::get_ref()
{
    return ref.get_int();
}

int dap_key_message::get_nsg()
{
    return nsg.get_int();
}

int dap_key_message::get_pos(int seg)
{
    return pos[seg]->get_int();
}

int dap_key_message::get_siz(int seg)
{
    return siz[seg]->get_int();
}

void dap_key_message::set_ref(int r)
{
    ref.set_int(r);
    keymenu.set_bit(4);
}

void dap_key_message::set_knm(char *n)
{
    knm.set_string(n);
    keymenu.set_bit(5);
}
