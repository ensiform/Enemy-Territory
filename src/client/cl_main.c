/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Wolfenstein: Enemy Territory GPL Source Code (Wolf ET Source Code).  

Wolf ET Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Wolf ET Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wolf ET Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Wolf: ET Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Wolf ET Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// cl_main.c  -- client main loop

#include "client.h"
#include <limits.h>

#include "snd_local.h" // fretn

cvar_t  *cl_wavefilerecord;
cvar_t  *cl_nodelta;
cvar_t  *cl_debugMove;

cvar_t  *cl_noprint;
cvar_t  *cl_motd;
cvar_t  *cl_autoupdate;         // DHM - Nerve

cvar_t  *rcon_client_password;
cvar_t  *rconAddress;

cvar_t  *cl_timeout;
cvar_t  *cl_maxpackets;
cvar_t  *cl_packetdup;
cvar_t  *cl_timeNudge;
cvar_t  *cl_showTimeDelta;
cvar_t  *cl_freezeDemo;

cvar_t  *cl_shownet = NULL;     // NERVE - SMF - This is referenced in msg.c and we need to make sure it is NULL
cvar_t  *cl_shownuments;        // DHM - Nerve
cvar_t  *cl_visibleClients;     // DHM - Nerve
cvar_t  *cl_showSend;
cvar_t  *cl_showServerCommands; // NERVE - SMF
cvar_t  *cl_timedemo;
cvar_t	*cl_autoRecordDemo;

cvar_t	*cl_aviFrameRate;
cvar_t	*cl_aviMotionJpeg;
cvar_t	*cl_forceavidemo;

cvar_t	*cl_freelook;
cvar_t	*cl_sensitivity;

cvar_t	*cl_mouseAccel;
cvar_t	*cl_mouseAccelOffset;
cvar_t	*cl_mouseAccelStyle;
cvar_t	*cl_showMouseRate;

cvar_t	*m_pitch;
cvar_t	*m_yaw;
cvar_t	*m_forward;
cvar_t	*m_side;
cvar_t	*m_filter;

cvar_t	*cl_activeAction;


cvar_t	*cl_motdString;

cvar_t	*cl_allowDownload;
cvar_t  *cl_wwwDownload;
cvar_t	*cl_conXOffset;
cvar_t	*cl_conColor;
cvar_t	*cl_inGameVideo;

cvar_t	*cl_serverStatusResendTime;

// NERVE - SMF - localization
cvar_t  *cl_language;
cvar_t  *cl_debugTranslation;
// -NERVE - SMF
// DHM - Nerve :: Auto-Update
cvar_t  *cl_updateavailable;
cvar_t  *cl_updatefiles;
// DHM - Nerve

cvar_t  *cl_profile;
cvar_t  *cl_defaultProfile;

cvar_t  *cl_demorecording; // fretn
cvar_t  *cl_demofilename; // bani
cvar_t  *cl_demooffset; // bani

cvar_t  *cl_waverecording; //bani
cvar_t  *cl_wavefilename; //bani
cvar_t  *cl_waveoffset; //bani

cvar_t  *cl_packetloss; //bani
cvar_t  *cl_packetdelay;    //bani
extern qboolean sv_cheats;  //bani

cvar_t	*cl_lanForcePackets;

//cvar_t	*cl_guidServerUniq;

//cvar_t	*cl_dlURL;

clientActive_t		cl;
clientConnection_t	clc;
clientStatic_t		cls;
vm_t				*cgvm;

char				cl_reconnectArgs[ MAX_OSPATH ];
char				cl_oldGame[ MAX_QPATH ];
qboolean			cl_oldGameSet;
static	qboolean	noGameRestart = qfalse;

// Structure containing functions exported from refresh DLL
refexport_t	re;

ping_t	cl_pinglist[MAX_PINGREQUESTS];

typedef struct serverStatus_s
{
	char string[BIG_INFO_STRING];
	netadr_t address;
	int time, startTime;
	qboolean pending;
	qboolean print;
	qboolean retrieved;
} serverStatus_t;

serverStatus_t cl_serverStatusList[MAX_SERVERSTATUSREQUESTS];

// DHM - Nerve :: Have we heard from the auto-update server this session?
qboolean autoupdateChecked;
qboolean autoupdateStarted;
// TTimo : moved from char* to array (was getting the char* from va(), broke on big downloads)
char autoupdateFilename[MAX_QPATH];
// "updates" shifted from -7
#define AUTOUPDATE_DIR "ni]Zm^l"
#define AUTOUPDATE_DIR_SHIFT 7

static void CL_CheckForResend( void );
static void CL_ShowIP_f( void );
static void CL_ServerStatus_f( void );
static void CL_ServerStatusResponse( const netadr_t *from, msg_t *msg );
static void CL_ServerInfoPacket( const netadr_t *from, msg_t *msg );

void CL_SaveTranslations_f( void );
void CL_LoadTranslations_f( void );

// fretn
void CL_WriteWaveClose( void );
void CL_WavStopRecord_f( void );

/*
===============
CL_CDDialog

Called by Com_Error when a cd is needed
===============
*/
void CL_CDDialog( void ) {
	cls.cddialog = qtrue;   // start it next frame
}

void CL_PurgeCache( void ) {
	cls.doCachePurge = qtrue;
}

void CL_DoPurgeCache( void ) {
	if ( !cls.doCachePurge ) {
		return;
	}

	cls.doCachePurge = qfalse;

	if ( !com_cl_running ) {
		return;
	}

	if ( !com_cl_running->integer ) {
		return;
	}

	if ( !cls.rendererStarted ) {
		return;
	}

	re.purgeCache();
}

/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
======================
CL_AddReliableCommand

The given command will be transmitted to the server, and is gauranteed to
not have future usercmd_t executed before it is executed
======================
*/
void CL_AddReliableCommand( const char *cmd, qboolean isDisconnectCmd ) {
	int		index;
	int		unacknowledged = clc.reliableSequence - clc.reliableAcknowledge;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// also leave one slot open for the disconnect command in this case.
	
	if ((isDisconnectCmd && unacknowledged > MAX_RELIABLE_COMMANDS) ||
	    (!isDisconnectCmd && unacknowledged >= MAX_RELIABLE_COMMANDS))
	{
		if( com_errorEntered )
			return;
		else
			Com_Error(ERR_DROP, "Client command overflow");
	}

	clc.reliableSequence++;
	index = clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( clc.reliableCommands[ index ], cmd, sizeof( clc.reliableCommands[ index ] ) );
}


/*
=======================================================================

CLIENT SIDE DEMO RECORDING

=======================================================================
*/

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/
static void CL_WriteDemoMessage( msg_t *msg, int headerBytes ) {
	int		len, swlen;

	// write the packet sequence
	len = clc.serverMessageSequence;
	swlen = LittleLong( len );
	FS_Write( &swlen, 4, clc.recordfile );

	// skip the packet sequencing information
	len = msg->cursize - headerBytes;
	swlen = LittleLong(len);
	FS_Write( &swlen, 4, clc.recordfile );
	FS_Write( msg->data + headerBytes, len, clc.recordfile );
}


/*
====================
CL_StopRecording_f

stop recording a demo
====================
*/
void CL_StopRecord_f( void ) {

	if ( clc.recordfile != FS_INVALID_HANDLE ) {
		char tempName[MAX_OSPATH];
		char finalName[MAX_OSPATH];
		int protocol;
		int	len;

		// finish up
		len = -1;
		FS_Write( &len, 4, clc.recordfile );
		FS_Write( &len, 4, clc.recordfile );
		FS_FCloseFile( clc.recordfile );
		clc.recordfile = FS_INVALID_HANDLE;

		// select proper extension
		if ( clc.dm84compat || clc.demoplaying )
			protocol = PROTOCOL_VERSION;
		else
			protocol = NEW_PROTOCOL_VERSION;

		Com_sprintf( finalName, sizeof( finalName ), "%s.%s%d", clc.recordName, DEMOEXT, protocol );
		Com_sprintf( tempName, sizeof( tempName ), "%s.tmp", clc.recordName );

		FS_Rename( tempName, finalName );
	}

	if ( !clc.demorecording ) {
		Com_Printf( "Not recording a demo.\n" );
	} else {
		Com_Printf( "Stopped demo recording.\n" );
	}

	clc.demorecording = qfalse;
	Cvar_Set( "cl_demorecording", "0" ); // fretn
	Cvar_Set( "cl_demofilename", "" ); // bani
	Cvar_Set( "cl_demooffset", "0" ); // bani
}

/*
====================
CL_WriteServerCommands
====================
*/
static void CL_WriteServerCommands( msg_t *msg ) {
	int i;

	if ( clc.demoCommandSequence < clc.serverCommandSequence ) {

		// do not write more than MAX_RELIABLE_COMMANDS
		if ( clc.serverCommandSequence - clc.demoCommandSequence > MAX_RELIABLE_COMMANDS )
			clc.demoCommandSequence = clc.serverCommandSequence - MAX_RELIABLE_COMMANDS;

		for ( i = clc.demoCommandSequence + 1 ; i <= clc.serverCommandSequence; i++ ) {
			MSG_WriteByte( msg, svc_serverCommand );
			MSG_WriteLong( msg, i );
			MSG_WriteString( msg, clc.serverCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
	}

	clc.demoCommandSequence = clc.serverCommandSequence;
}


/*
====================
CL_WriteGamestate
====================
*/
static void CL_WriteGamestate( qboolean initial ) 
{
	byte		bufData[MAX_MSGLEN];
	char		*s;
	msg_t		msg;
	int			i;
	int			len;
	entityState_t	*ent;
	entityState_t	nullstate;

	// write out the gamestate message
	MSG_Init( &msg, bufData, sizeof(bufData));
	MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &msg, clc.reliableSequence );

	if ( initial ) {
		clc.demoMessageSequence = 1;
		clc.demoCommandSequence = clc.serverCommandSequence;
	} else {
		CL_WriteServerCommands( &msg );
	}
	
	clc.demoDeltaNum = 0; // reset delta for next snapshot
	
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, clc.serverCommandSequence );

	// configstrings
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( !cl.gameState.stringOffsets[i] ) {
			continue;
		}
		s = cl.gameState.stringData + cl.gameState.stringOffsets[i];
		MSG_WriteByte( &msg, svc_configstring );
		MSG_WriteShort( &msg, i );
		MSG_WriteBigString( &msg, s );
	}

	// baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( i = 0; i < MAX_GENTITIES ; i++ ) {
		if ( !cl.baselineUsed[ i ] )
			continue;
		ent = &cl.entityBaselines[ i ];
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, ent, qtrue );
	}

	// finalize message
	MSG_WriteByte( &msg, svc_EOF );
	
	// finished writing the gamestate stuff

	// write the client num
	MSG_WriteLong( &msg, clc.clientNum );

	// write the checksum feed
	MSG_WriteLong( &msg, clc.checksumFeed );

	// finished writing the client packet
	MSG_WriteByte( &msg, svc_EOF );

	// write it to the demo file
	if ( clc.demoplaying )
		len = LittleLong( clc.demoMessageSequence - 1 );
	else
		len = LittleLong( clc.serverMessageSequence - 1 );

	FS_Write( &len, 4, clc.recordfile );

	len = LittleLong( msg.cursize );
	FS_Write( &len, 4, clc.recordfile );
	FS_Write( msg.data, msg.cursize, clc.recordfile );
}


/*
=============
CL_EmitPacketEntities
=============
*/
static void CL_EmitPacketEntities( clSnapshot_t *from, clSnapshot_t *to, msg_t *msg, entityState_t *oldents ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->numEntities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->numEntities || oldindex < from_num_entities ) {
		if ( newindex >= to->numEntities ) {
			newnum = MAX_GENTITIES+1;
		} else {
			newent = &cl.parseEntities[(to->parseEntitiesNum + newindex) % MAX_PARSE_ENTITIES];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			//oldent = &cl.parseEntities[(from->parseEntitiesNum + oldindex) % MAX_PARSE_ENTITIES];
			oldent = &oldents[ oldindex ];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emited if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, &cl.entityBaselines[newnum], newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, NULL, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}


/*
====================
CL_WriteSnapshot
====================
*/
static void CL_WriteSnapshot( void ) {

	static	clSnapshot_t saved_snap;
	static entityState_t saved_ents[ MAX_SNAPSHOT_ENTITIES ]; 

	clSnapshot_t *snap, *oldSnap; 
	byte	bufData[MAX_MSGLEN];
	msg_t	msg;
	int		i, len;

	snap = &cl.snapshots[ cl.snap.messageNum & PACKET_MASK ]; // current snapshot
	//if ( !snap->valid ) // should never happen?
	//	return;

	if ( clc.demoDeltaNum == 0 ) {
		oldSnap = NULL;
	} else {
		oldSnap = &saved_snap;
	}

	MSG_Init( &msg, bufData, sizeof( bufData ) );
	MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &msg, clc.reliableSequence );

	// Write all pending server commands
	CL_WriteServerCommands( &msg );
	
	MSG_WriteByte( &msg, svc_snapshot );
	MSG_WriteLong( &msg, snap->serverTime ); // sv.time
	MSG_WriteByte( &msg, clc.demoDeltaNum ); // 0 or 1
	MSG_WriteByte( &msg, snap->snapFlags );  // snapFlags
	MSG_WriteByte( &msg, snap->areabytes );  // areabytes
	MSG_WriteData( &msg, snap->areamask, snap->areabytes );
	if ( oldSnap )
		MSG_WriteDeltaPlayerstate( &msg, &oldSnap->ps, &snap->ps );
	else
		MSG_WriteDeltaPlayerstate( &msg, NULL, &snap->ps );

	CL_EmitPacketEntities( oldSnap, snap, &msg, saved_ents );

	// finished writing the client packet
	MSG_WriteByte( &msg, svc_EOF );

	// write it to the demo file
	if ( clc.demoplaying )
		len = LittleLong( clc.demoMessageSequence );
	else
		len = LittleLong( clc.serverMessageSequence );
	FS_Write( &len, 4, clc.recordfile );

	len = LittleLong( msg.cursize );
	FS_Write( &len, 4, clc.recordfile );
	FS_Write( msg.data, msg.cursize, clc.recordfile );

	// save last sent state so if there any need - we can skip any further incoming messages
	for ( i = 0; i < snap->numEntities; i++ )
		saved_ents[ i ] = cl.parseEntities[ (snap->parseEntitiesNum + i) % MAX_PARSE_ENTITIES ];

	saved_snap = *snap;
	saved_snap.parseEntitiesNum = 0;

	clc.demoMessageSequence++;
	clc.demoDeltaNum = 1;
}


/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static void CL_Record_f( void ) {
	char		demoName[MAX_OSPATH];
	char		name[MAX_OSPATH];
	char		demoExt[16];
	const char	*ext;
	int			number;

	if ( Cmd_Argc() > 2 ) {
		Com_Printf( "record <demoname>\n" );
		return;
	}

	if ( clc.demorecording ) {
		Com_Printf( "Already recording.\n" );
		return;
	}

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "You must be in a level to record.\n" );
		return;
	}


	// ATVI Wolfenstein Misc #479 - changing this to a warning
	// sync 0 doesn't prevent recording, so not forcing it off .. everyone does g_sync 1 ; record ; g_sync 0 ..
	//if ( NET_IsLocalAddress( &clc.serverAddress ) && !Cvar_VariableIntegerValue( "g_synchronousClients" ) ) {
	//	Com_Printf (S_COLOR_YELLOW "WARNING: You should set 'g_synchronousClients 1' for smoother demo recording\n");
	//}

	if ( Cmd_Argc() == 2 ) {
		// explicit demo name specified
		Q_strncpyz( demoName, Cmd_Argv( 1 ), sizeof( demoName ) );
		ext = COM_GetExtension( demoName );
		if ( *ext ) {
			sprintf( demoExt, "%s%d", DEMOEXT, PROTOCOL_VERSION );
			if ( Q_stricmp( ext, demoExt ) == 0 ) {
				*(strrchr( demoName, '.' )) = '\0';
			} else {
				// check both protocols
				sprintf( demoExt, "%s%d", DEMOEXT, NEW_PROTOCOL_VERSION );
				if ( Q_stricmp( ext, demoExt ) == 0 ) {
					*(strrchr( demoName, '.' )) = '\0';
				}
			}
		}
		Com_sprintf( name, sizeof( name ), "demos/%s", demoName );
	} else {

		// scan for a free demo name
		for ( number = 0 ; number <= 9999 ; number++ ) {
			Com_sprintf( name, sizeof( name ), "demos/demo%04d.%s%d", number, DEMOEXT, PROTOCOL_VERSION );
			if ( !FS_FileExists( name ) ) {
				// check both protocols
				Com_sprintf( name, sizeof( name ), "demos/demo%04d.%s%d", number, DEMOEXT, NEW_PROTOCOL_VERSION );
				if ( !FS_FileExists( name ) ) {
					break;	// file doesn't exist
				}
			}
		}
		Com_sprintf( name, sizeof( name ), "demos/demo%04d", number );
	}

	// save desired filename without extension
	Q_strncpyz( clc.recordName, name, sizeof( clc.recordName ) );

	Com_Printf( "recording to %s.\n", name );

	// start new record with temporary extension
	Q_strcat( name, sizeof( name ), ".tmp" );

	// open the demo file
	clc.recordfile = FS_FOpenFileWrite( name );
	if ( clc.recordfile == FS_INVALID_HANDLE ) {
		Com_Printf( "ERROR: couldn't open.\n" );
		clc.recordName[0] = '\0';
		return;
	}

	clc.demorecording = qtrue;

	Com_TruncateLongString( clc.recordNameShort, clc.recordName );

	// don't start saving messages until a non-delta compressed message is received
	clc.demowaiting = qtrue;

	// we will rename record to dm_84 or dm_85 depending from this flag
	clc.dm84compat = qtrue;

	// write out the gamestate message
	CL_WriteGamestate( qtrue );

	// the rest of the demo file will be copied from net messages
}


/*
====================
CL_CompleteRecordName
====================
*/
static void CL_CompleteRecordName( char *args, int argNum )
{
	if( argNum == 2 )
	{
		char demoExt[ 16 ];

		Com_sprintf( demoExt, sizeof( demoExt ), ".dm_%d", PROTOCOL_VERSION );
		Field_CompleteFilename( "demos", demoExt, qtrue, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
=======================================================================

CLIENT SIDE DEMO PLAYBACK

=======================================================================
*/

/*
=================
CL_DemoCompleted
=================
*/
static void CL_DemoCompleted( void ) {
	if (cl_timedemo && cl_timedemo->integer) {
		int	time;
		
		time = Sys_Milliseconds() - clc.timeDemoStart;
		if ( time > 0 ) {
			Com_Printf( "%i frames, %3.*f seconds: %3.1f fps\n", clc.timeDemoFrames,
			time > 10000 ? 1 : 2, time/1000.0, clc.timeDemoFrames*1000.0 / time );
		}
	}

	// fretn
	//if ( clc.waverecording ) {
	//	CL_WriteWaveClose();
	//	clc.waverecording = qfalse;
	//}

	CL_Disconnect( qtrue );
	CL_ShutdownCGame();
	CL_NextDemo();
}


/*
=================
CL_ReadDemoMessage
=================
*/
void CL_ReadDemoMessage( void ) {
	int			r;
	msg_t		buf;
	byte		bufData[ MAX_MSGLEN ];
	int			s;

	if ( clc.demofile == FS_INVALID_HANDLE ) {
		CL_DemoCompleted();
		return;
	}

	// get the sequence number
	r = FS_Read( &s, 4, clc.demofile );
	if ( r != 4 ) {
		CL_DemoCompleted();
		return;
	}
	clc.serverMessageSequence = LittleLong( s );

	// init the message
	MSG_Init( &buf, bufData, sizeof( bufData ) );

	// get the length
	r = FS_Read( &buf.cursize, 4, clc.demofile );
	if ( r != 4 ) {
		CL_DemoCompleted ();
		return;
	}
	buf.cursize = LittleLong( buf.cursize );
	if ( buf.cursize == -1 ) {
		CL_DemoCompleted();
		return;
	}
	if ( buf.cursize > buf.maxsize ) {
		Com_Error (ERR_DROP, "CL_ReadDemoMessage: demoMsglen > MAX_MSGLEN");
	}
	r = FS_Read( buf.data, buf.cursize, clc.demofile );
	if ( r != buf.cursize ) {
		Com_Printf( "Demo file was truncated.\n");
		CL_DemoCompleted();
		return;
	}

	clc.lastPacketTime = cls.realtime;
	buf.readcount = 0;

	clc.demoCommandSequence = clc.serverCommandSequence;

	CL_ParseServerMessage( &buf );

	if ( clc.demorecording ) {
		// track changes and write new message	
		if ( clc.eventMask & EM_GAMESTATE ) {
			CL_WriteGamestate( qfalse );
			// nothing should came after gamestate in current message
		} else if ( clc.eventMask & (EM_SNAPSHOT|EM_COMMAND) ) {
			CL_WriteSnapshot();
		}
	}
}


/*
====================
CL_WalkDemoExt
====================
*/
static int CL_WalkDemoExt( const char *arg, char *name, fileHandle_t *handle )
{
	int i;
	
	*handle = FS_INVALID_HANDLE;
	i = 0;

	while ( demo_protocols[ i ] )
	{
		Com_sprintf( name, MAX_OSPATH, "demos/%s.dm_%d", arg, demo_protocols[ i ] );
		FS_FOpenFileRead( name, handle, qtrue );
		if ( *handle != FS_INVALID_HANDLE )
		{
			Com_Printf( "Demo file: %s\n", name );
			return demo_protocols[ i ];
		}
		else
			Com_Printf( "Not found: %s\n", name );
		i++;
	}
	return -1;
}


/*
====================
CL_DemoExtCallback
====================
*/
static qboolean CL_DemoNameCallback_f( const char *filename, int length ) 
{
	int version;

	if ( length < 7 || Q_stricmpn( filename + length - 6, ".dm_", 4 ) )
		return qfalse;

	version = atoi( filename + length - 2 );
	if ( version < 84 || version > NEW_PROTOCOL_VERSION )
		return qfalse;

	return qtrue;
}


/*
====================
CL_CompleteDemoName
====================
*/
static void CL_CompleteDemoName( char *args, int argNum )
{
	if( argNum == 2 )
	{
		FS_SetFilenameCallback( CL_DemoNameCallback_f );
		Field_CompleteFilename( "demos", ".dm_??", qfalse, FS_MATCH_ANY | FS_MATCH_STICK );
		FS_SetFilenameCallback( NULL );
	}
}


/*
====================
CL_PlayDemo_f

demo <demoname>

====================
*/
static void CL_PlayDemo_f( void ) {
	char		name[MAX_OSPATH];
	char		*arg, *ext_test;
	int			protocol, i;
	char		retry[MAX_OSPATH];
	const char	*shortname, *slash;
	fileHandle_t hFile;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "demo <demoname>\n" );
		return;
	}

	// open the demo file
	arg = Cmd_Argv(1);

	// check for an extension .dm_?? (?? is protocol)
	// check for an extension .DEMOEXT_?? (?? is protocol)
	ext_test = strrchr(arg, '.');
	if ( ext_test && !Q_stricmpn(ext_test + 1, DEMOEXT, ARRAY_LEN(DEMOEXT) - 1) )
	{
		protocol = atoi(ext_test + ARRAY_LEN(DEMOEXT));

		for( i = 0; demo_protocols[ i ]; i++ )
		{
			if ( demo_protocols[ i ] == protocol )
				break;
		}

		if ( demo_protocols[ i ] /* || protocol == com_protocol->integer  || protocol == com_legacyprotocol->integer */ )
		{
			Com_sprintf(name, sizeof(name), "demos/%s", arg);
			FS_FOpenFileRead( name, &hFile, qtrue );
		}
		else
		{
			size_t len;

			Com_Printf("Protocol %d not supported for demos\n", protocol );
			len = ext_test - arg;

			if(len >= ARRAY_LEN(retry))
				len = ARRAY_LEN(retry) - 1;

			Q_strncpyz( retry, arg, len + 1);
			retry[len] = '\0';
			protocol = CL_WalkDemoExt( retry, name, &hFile );
		}
	}
	else
		protocol = CL_WalkDemoExt( arg, name, &hFile );
	
	if ( hFile == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_YELLOW "couldn't open %s\n", name );
		return;
	}

	FS_FCloseFile( hFile ); 
	hFile = FS_INVALID_HANDLE;

	// make sure a local server is killed
	// 2 means don't force disconnect of local client
	Cvar_Set( "sv_killserver", "2" );

	CL_Disconnect( qtrue );

	// clc.demofile will be closed during CL_Disconnect so reopen it
	if ( FS_FOpenFileRead( name, &clc.demofile, qtrue ) == -1 ) 
	{
		// drop this time
		Com_Error( ERR_DROP, "couldn't open %s\n", name );
		return;
	}

	if ( (slash = strrchr( name, '/' )) != NULL )
		shortname = slash + 1;
	else
		shortname = name;

	Q_strncpyz( clc.demoName, shortname, sizeof( clc.demoName ) );

	Con_Close();

	cls.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	Q_strncpyz( cls.servername, shortname, sizeof( cls.servername ) );

	if ( protocol < NEW_PROTOCOL_VERSION )
		clc.compat = qtrue;
	else
		clc.compat = qfalse;

	// read demo messages until connected
	while ( cls.state >= CA_CONNECTED && cls.state < CA_PRIMED ) {
		CL_ReadDemoMessage();
	}

	// don't get the first snapshot this frame, to prevent the long
	// time from the gamestate load from messing causing a time skip
	clc.firstDemoFrameSkipped = qfalse;
}


/*
====================
CL_StartDemoLoop

Closing the main menu will restart the demo loop
====================
*/
void CL_StartDemoLoop( void ) {
	// start the demo loop again
	Cbuf_AddText ("d1\n");
	Key_SetCatcher( 0 );
}


/*
==================
CL_NextDemo

Called when a demo or cinematic finishes
If the "nextdemo" cvar is set, that command will be issued
==================
*/
void CL_NextDemo( void ) {
	char v[MAX_STRING_CHARS];

	Q_strncpyz( v, Cvar_VariableString( "nextdemo" ), sizeof( v ) );
	Com_DPrintf("CL_NextDemo: %s\n", v );
	if (!v[0]) {
		return;
	}

	Cvar_Set( "nextdemo","" );
	Cbuf_AddText( v );
	Cbuf_AddText( "\n" );
	Cbuf_Execute();
}


//======================================================================

/*
=====================
CL_ShutdownVMs
=====================
*/
static void CL_ShutdownVMs( void )
{
	CL_ShutdownCGame();
	CL_ShutdownUI();
}


/*
=====================
CL_ShutdownAll
=====================
*/
void CL_ShutdownAll( void ) {

	// clear sounds
	S_DisableSounds();

	// download subsystem
	DL_Shutdown();

	// shutdown VMs
	CL_ShutdownVMs();

	// shutdown sound system before renderer -EC-
	S_Shutdown();
	cls.soundStarted = qfalse;

	// shutdown the renderer
	if ( re.Shutdown ) {
		re.Shutdown( qfalse );      // don't destroy window or context
	}

	if ( re.purgeCache ) {
		CL_DoPurgeCache();
	}

	cls.uiStarted = qfalse;
	cls.cgameStarted = qfalse;
	cls.rendererStarted = qfalse;
	cls.soundRegistered = qfalse;
}


/*
=================
CL_ClearMemory
=================
*/
void CL_ClearMemory( void ) {
	// if not running a server clear the whole hunk
	if ( !com_sv_running->integer ) {
		// clear the whole hunk
		Hunk_Clear();
		// clear collision map data
		CM_ClearMap();
	} else {
		// clear all the client data on the hunk
		Hunk_ClearToMark();
	}
}


/*
=================
CL_FlushMemory

Called by CL_MapLoading, CL_Connect_f, CL_PlayDemo_f, and CL_ParseGamestate the only
ways a client gets into a game
Also called by Com_Error
=================
*/
void CL_FlushMemory( void ) {

	// shutdown all the client stuff
	CL_ShutdownAll();

	CL_ClearMemory();

	CL_StartHunkUsers();
}

/*
=====================
CL_MapLoading

A local server is starting to load a map, so update the
screen to let the user know about it, then dump all client
memory on the hunk from cgame, ui, and renderer
=====================
*/
void CL_MapLoading( void ) {
  	if ( com_dedicated->integer ) {
  		cls.state = CA_DISCONNECTED;
 		Key_SetCatcher( KEYCATCH_CONSOLE );
  		return;
  	}

	if ( !com_cl_running->integer ) {
		return;
	}

	Con_Close();
	Key_SetCatcher( 0 );

	// if we are already connected to the local host, stay connected
	if ( cls.state >= CA_CONNECTED && !Q_stricmp( cls.servername, "localhost" ) ) {
		cls.state = CA_CONNECTED;		// so the connect screen is drawn
		Com_Memset( cls.updateInfoString, 0, sizeof( cls.updateInfoString ) );
		Com_Memset( clc.serverMessage, 0, sizeof( clc.serverMessage ) );
		Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );
		clc.lastPacketSentTime = -9999;
		SCR_UpdateScreen();
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
		CL_Disconnect( qtrue );
		Q_strncpyz( cls.servername, "localhost", sizeof(cls.servername) );
		cls.state = CA_CHALLENGING;		// so the connect screen is drawn
		Key_SetCatcher( 0 );
		/* Execute next line twice, so that the connect image gets written into both, front- and
		 * back buffer. This is necessary to prevent a flashing screen on map startup, as the UI gets
		 * killed for a short time and cannot update the screen. */
		SCR_UpdateScreen();
		SCR_UpdateScreen();
		clc.connectTime = -RETRANSMIT_TIMEOUT;
		NET_StringToAdr( cls.servername, &clc.serverAddress, NA_UNSPEC );
		// we don't need a challenge on the localhost

		CL_CheckForResend();
	}
}

/*
=====================
CL_ClearState

Called before parsing a gamestate
=====================
*/
void CL_ClearState (void) {

//	S_StopAllSounds();

	Com_Memset( &cl, 0, sizeof( cl ) );
}

/*
=====================
CL_ClearStaticDownload
Clear download information that we keep in cls (disconnected download support)
=====================
*/
void CL_ClearStaticDownload( void ) {
	assert( !cls.bWWWDlDisconnected ); // reset before calling
	cls.downloadRestart = qfalse;
	cls.downloadTempName[0] = '\0';
	cls.downloadName[0] = '\0';
	cls.originalDownloadName[0] = '\0';
}

/*
=====================
CL_ResetOldGame
=====================
*/
void CL_ResetOldGame( void ) 
{
	cl_oldGameSet = qfalse;
	cl_oldGame[0] = '\0';
}


/*
=====================
CL_RestoreOldGame

change back to previous fs_game
=====================
*/
static void CL_RestoreOldGame( void )
{
	if ( cl_oldGameSet )
	{
		cl_oldGameSet = qfalse;
		Cvar_Set2( "fs_game", cl_oldGame, qtrue );
		FS_ConditionalRestart( clc.checksumFeed, qtrue );
	}
}


/*
=====================
CL_Disconnect

Called when a connection, demo, or cinematic is being terminated.
Goes from a connected state to either a menu state or a console state
Sends a disconnect message to the server
This is also called on Com_Error and Com_Quit, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( qboolean showMainMenu ) {
	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	// shutting down the client so enter full screen ui mode
	Cvar_Set( "r_uiFullScreen", "1" );

	// Stop demo recording
	if ( clc.demorecording ) {
		CL_StopRecord_f();
	}

	// Stop demo playback
	if ( clc.demofile != FS_INVALID_HANDLE ) {
		FS_FCloseFile( clc.demofile );
		clc.demofile = FS_INVALID_HANDLE;
	}

	// Finish downloads
	if ( !cls.bWWWDlDisconnected ) {
		if ( clc.download != FS_INVALID_HANDLE ) {
			FS_FCloseFile( clc.download );
			clc.download = FS_INVALID_HANDLE;
		}
		*clc.downloadTempName = *clc.downloadName = '\0';
		Cvar_Set( "cl_downloadName", "" );

		autoupdateStarted = qfalse;
		autoupdateFilename[0] = '\0';
	}
	if ( uivm && showMainMenu ) {
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE );
	}

	SCR_StopCinematic();
	S_ClearSoundBuffer( qtrue );  //----(SA)	modified

	// send a disconnect message to the server
	// send it a few times in case one is dropped
	if ( cls.state >= CA_CONNECTED ) {
		CL_AddReliableCommand( "disconnect", qtrue );
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}
	
	// Remove pure paks
	FS_PureServerSetLoadedPaks( "", "" );
	FS_PureServerSetReferencedPaks( "", "" );
	
	CL_ClearState();

	// wipe the client connection
	Com_Memset( &clc, 0, sizeof( clc ) );

	if ( !cls.bWWWDlDisconnected ) {
		CL_ClearStaticDownload();
	}
	cls.state = CA_DISCONNECTED;

	// allow cheats locally
	Cvar_Set( "sv_cheats", "1" );

	// not connected to a pure server anymore
	cl_connectedToPureServer = qfalse;
	
	// Stop recording any video
	if( CL_VideoRecording() ) {
		// Finish rendering current frame
		SCR_UpdateScreen();
		CL_CloseAVI();
	}

	// show_bug.cgi?id=589
	// don't try a restart if uivm is NULL, as we might be in the middle of a restart already
/*	if ( uivm && cls.state > CA_DISCONNECTED ) {
		// restart the UI
		cls.state = CA_DISCONNECTED;

		// shutdown the UI
		CL_ShutdownUI();

		// init the UI
		CL_InitUI();
	} else {
		cls.state = CA_DISCONNECTED;
	}*/
	
	if ( noGameRestart )
		noGameRestart = qfalse;
	else
		CL_RestoreOldGame();
}


/*
===================
CL_ForwardCommandToServer

adds the current command line as a clientCommand
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void CL_ForwardCommandToServer( const char *string ) {
	char	*cmd;

	cmd = Cmd_Argv(0);

	// ignore key up commands
	if ( cmd[0] == '-' ) {
		return;
	}

	if ( clc.demoplaying || cls.state < CA_CONNECTED || cmd[0] == '+' ) {
		Com_Printf( "Unknown command \"%s" S_COLOR_WHITE "\"\n", cmd );
		return;
	}

	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( string, qfalse );
	} else {
		CL_AddReliableCommand( cmd, qfalse );
	}
}

/*
===================
CL_RequestMotd

===================
*/
void CL_RequestMotd( void ) {
	char info[MAX_INFO_STRING];

	if ( !cl_motd->integer ) {
		return;
	}
	Com_Printf( "Resolving %s\n", MOTD_SERVER_NAME );
	if ( !NET_StringToAdr( MOTD_SERVER_NAME, &cls.updateServer, NA_IP ) ) {
		Com_Printf( "Couldn't resolve address\n" );
		return;
	}
	cls.updateServer.port = BigShort( PORT_MOTD );
	Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", MOTD_SERVER_NAME,
				cls.updateServer.ip[0], cls.updateServer.ip[1],
				cls.updateServer.ip[2], cls.updateServer.ip[3],
				BigShort( cls.updateServer.port ) );

	info[0] = 0;
	Com_sprintf( cls.updateChallenge, sizeof( cls.updateChallenge ), "%i", rand() );

	Info_SetValueForKey( info, "challenge", cls.updateChallenge );
	Info_SetValueForKey( info, "renderer", cls.glconfig.renderer_string );
	Info_SetValueForKey( info, "version", com_version->string );

	NET_OutOfBandPrint( NS_CLIENT, &cls.updateServer, "getmotd \"%s\"\n", info );
}

#ifdef AUTHORIZE_SUPPORT

/*
===================
CL_RequestAuthorization

Authorization server protocol
-----------------------------

All commands are text in Q3 out of band packets (leading 0xff 0xff 0xff 0xff).

Whenever the client tries to get a challenge from the server it wants to
connect to, it also blindly fires off a packet to the authorize server:

getKeyAuthorize <challenge> <cdkey>

cdkey may be "demo"


#OLD The authorize server returns a:
#OLD
#OLD keyAthorize <challenge> <accept | deny>
#OLD
#OLD A client will be accepted if the cdkey is valid and it has not been used by any other IP
#OLD address in the last 15 minutes.


The server sends a:

getIpAuthorize <challenge> <ip>

The authorize server returns a:

ipAuthorize <challenge> <accept | deny | demo | unknown >

A client will be accepted if a valid cdkey was sent by that ip (only) in the last 15 minutes.
If no response is received from the authorize server after two tries, the client will be let
in anyway.
===================
*/
void CL_RequestAuthorization( void ) {
	char nums[64];
	int i, j, l;
	cvar_t  *fs;

	if ( !cls.authorizeServer.port ) {
		Com_Printf( "Resolving %s\n", AUTHORIZE_SERVER_NAME );
		if ( !NET_StringToAdr( AUTHORIZE_SERVER_NAME, &cls.authorizeServer, NA_IP ) ) {
			Com_Printf( "Couldn't resolve address\n" );
			return;
		}

		cls.authorizeServer.port = BigShort( PORT_AUTHORIZE );
		Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", AUTHORIZE_SERVER_NAME,
					cls.authorizeServer.ip[0], cls.authorizeServer.ip[1],
					cls.authorizeServer.ip[2], cls.authorizeServer.ip[3],
					BigShort( cls.authorizeServer.port ) );
	}
	if ( cls.authorizeServer.type == NA_BAD ) {
		return;
	}

	if ( Cvar_VariableValue( "fs_restrict" ) ) {
		Q_strncpyz( nums, "ettest", sizeof( nums ) );
	} else {
		// only grab the alphanumeric values from the cdkey, to avoid any dashes or spaces
		j = 0;
		l = strlen( cl_cdkey );
		if ( l > 32 ) {
			l = 32;
		}
		for ( i = 0 ; i < l ; i++ ) {
			if ( ( cl_cdkey[i] >= '0' && cl_cdkey[i] <= '9' )
				 || ( cl_cdkey[i] >= 'a' && cl_cdkey[i] <= 'z' )
				 || ( cl_cdkey[i] >= 'A' && cl_cdkey[i] <= 'Z' )
				 ) {
				nums[j] = cl_cdkey[i];
				j++;
			}
		}
		nums[j] = 0;
	}

	fs = Cvar_Get( "cl_anonymous", "0", CVAR_INIT | CVAR_SYSTEMINFO );
	NET_OutOfBandPrint( NS_CLIENT, &cls.authorizeServer, "getKeyAuthorize %i %s", fs->integer, nums );
}
#endif // AUTHORIZE_SUPPORT

/*
======================================================================

CONSOLE COMMANDS

======================================================================
*/

/*
==================
CL_ForwardToServer_f
==================
*/
static void CL_ForwardToServer_f( void ) {
	if ( cls.state != CA_ACTIVE || clc.demoplaying ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}

	// don't forward the first argument
	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( Cmd_Args(), qfalse );
	}
}

/*
==================
CL_Disconnect_f
==================
*/
void CL_Disconnect_f( void ) {
	SCR_StopCinematic();
	Cvar_Set( "savegame_loading", "0" );
	Cvar_Set( "g_reloading", "0" );
	if ( cls.state != CA_DISCONNECTED && cls.state != CA_CINEMATIC ) {
		Com_Error( ERR_DISCONNECT, "Disconnected from server" );
	}
}


/*
================
CL_Reconnect_f

================
*/
void CL_Reconnect_f( void ) {
	if ( !strlen( cl_reconnectArgs ) )
		return;
	Cbuf_AddText( va("connect %s\n", cl_reconnectArgs ) );
}

/*
================
CL_Connect_f

================
*/
void CL_Connect_f( void ) {
	netadrtype_t family;
	netadr_t	addr;
	char	buffer[ sizeof(cls.servername) ];  // same length as cls.servername
	char	cmd_args[ sizeof(cl_reconnectArgs) ];
	char	*server;	
	const char	*serverString;
	int		len;
	int		argc;

	argc = Cmd_Argc();
	family = NA_UNSPEC;

	if ( argc != 2 && argc != 3 ) {
		Com_Printf( "usage: connect [-4|-6] server\n");
		return;	
	}
	
	if( argc == 2 ) {
		server = Cmd_Argv(1);
	} else {
		if( !strcmp( Cmd_Argv(1), "-4" ) )
			family = NA_IP;
		else if( !strcmp( Cmd_Argv(1), "-6" ) )
			family = NA_IP6;
		else
			Com_Printf( "warning: only -4 or -6 as address type understood.\n" );
		
		server = Cmd_Argv(2);
	}

	Q_strncpyz( buffer, server, sizeof( buffer ) );
	server = buffer;

	// skip leading "et:/" in connection string
	if ( !Q_stricmpn( server, "et:/", 4 ) ) {
		server += 5;
	}

	// skip all slash prefixes
	while ( *server == '/' ) {
		server++;
	}

	len = strlen( server );
	if ( len <= 0 ) {
		return;
	}

	// some programs may add ending slash
	if ( server[len-1] == '/' ) {
		server[len-1] = '\0';
	}

	if ( !*server ) {
		return;
	}

	// try resolve remote server first
	if ( !NET_StringToAdr( server, &addr, family ) ) {
		Com_Printf( S_COLOR_YELLOW "Bad server address - %s\n", server );
		//cls.state = CA_DISCONNECTED;
		Cvar_Set( "ui_connecting", "0" );
		return;
	}

	Q_strncpyz( cmd_args, Cmd_Args(), sizeof( cmd_args ) );

	S_StopAllSounds();      // NERVE - SMF

	// starting to load a map so we get out of full screen ui mode
	Cvar_Set( "r_uiFullScreen", "0" );
	Cvar_Set( "ui_connecting", "1" );

	// fire a message off to the motd server
	CL_RequestMotd();

	// clear any previous "server full" type messages
	clc.serverMessage[0] = '\0';

	// if running a local server, kill it
	if ( com_sv_running->integer && !strcmp( server, "localhost" ) ) {
		SV_Shutdown( "Server quit" );
	}

	// make sure a local server is killed
	Cvar_Set( "sv_killserver", "1" );
	SV_Frame( 0 );

	noGameRestart = qtrue;
	CL_Disconnect( qtrue );
	Con_Close();

	Q_strncpyz( cls.servername, server, sizeof( cls.servername ) );

	// save arguments for reconnect
	strcpy( cl_reconnectArgs, cmd_args );

	// copy resolved address 
	clc.serverAddress = addr;

	if (clc.serverAddress.port == 0) {
		clc.serverAddress.port = BigShort( PORT_SERVER );
	}

	serverString = NET_AdrToStringwPort( &clc.serverAddress );

	Com_Printf( "%s resolved to %s\n", cls.servername, serverString);

	// if we aren't playing on a lan, we need to authenticate
	// with the cd key
	if ( NET_IsLocalAddress( &clc.serverAddress ) ) {
		cls.state = CA_CHALLENGING;
	} else {
		cls.state = CA_CONNECTING;

		// Set a client challenge number that ideally is mirrored back by the server.
		clc.challenge = ((rand() << 16) ^ rand()) ^ Com_Milliseconds();
	}


	//Cvar_Set( "cl_avidemo", "0" );

	// show_bug.cgi?id=507
	// prepare to catch a connection process that would turn bad
	Cvar_Set( "com_errorDiagnoseIP", serverString );
	// ATVI Wolfenstein Misc #439
	// we need to setup a correct default for this, otherwise the first val we set might reappear
	Cvar_Set( "com_errorMessage", "" );

	Key_SetCatcher( 0 );
	clc.connectTime = -99999;   // CL_CheckForResend() will fire immediately
	clc.connectPacketCount = 0;

	// server connection string
	Cvar_Set( "cl_currentServerAddress", server );
	Cvar_Set( "cl_currentServerIP", serverString );

	// Gordon: um, couldnt this be handled
	// NERVE - SMF - reset some cvars
	Cvar_Set( "mp_playerType", "0" );
	Cvar_Set( "mp_currentPlayerType", "0" );
	Cvar_Set( "mp_weapon", "0" );
	Cvar_Set( "mp_team", "0" );
	Cvar_Set( "mp_currentTeam", "0" );

	Cvar_Set( "ui_limboOptions", "0" );
	Cvar_Set( "ui_limboPrevOptions", "0" );
	Cvar_Set( "ui_limboObjective", "0" );
	// -NERVE - SMF

}

#define MAX_RCON_MESSAGE 1024

/*
==================
CL_CompleteRcon
==================
*/
static void CL_CompleteRcon( char *args, int argNum )
{
	if( argNum == 2 )
	{
		// Skip "rcon "
		char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteCommand( p, qtrue, qtrue );
	}
}


/*
=====================
CL_Rcon_f

  Send the rest of the command line over as
  an unconnected command.
=====================
*/
void CL_Rcon_f( void ) {
	char	message[MAX_RCON_MESSAGE];
	netadr_t	to;

	if ( !rcon_client_password->string[0] ) {
		Com_Printf ("You must set 'rconPassword' before\n"
					"issuing an rcon command.\n");
		return;
	}

	message[0] = -1;
	message[1] = -1;
	message[2] = -1;
	message[3] = -1;
	message[4] = 0;

	Q_strcat (message, MAX_RCON_MESSAGE, "rcon ");

	Q_strcat (message, MAX_RCON_MESSAGE, rcon_client_password->string);
	Q_strcat (message, MAX_RCON_MESSAGE, " ");

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
	Q_strcat (message, MAX_RCON_MESSAGE, Cmd_Cmd()+5);

	if ( cls.state >= CA_CONNECTED ) {
		to = clc.netchan.remoteAddress;
	} else {
		if (!strlen(rconAddress->string)) {
			Com_Printf ("You must either be connected,\n"
						"or set the 'rconAddress' cvar\n"
						"to issue rcon commands\n");

			return;
		}
		NET_StringToAdr( rconAddress->string, &to, NA_UNSPEC );
		if (to.port == 0) {
			to.port = BigShort (PORT_SERVER);
		}
	}
	
	NET_SendPacket( NS_CLIENT, strlen(message)+1, message, &to );
}


/*
=================
CL_SendPureChecksums
=================
*/
void CL_SendPureChecksums( void ) {
	char cMsg[MAX_INFO_VALUE];

	// if we are pure we need to send back a command with our referenced pk3 checksums
	Com_sprintf(cMsg, sizeof(cMsg), "cp %d %s", cl.serverId, FS_ReferencedPakPureChecksums());

	CL_AddReliableCommand(cMsg, qfalse);
}

/*
=================
CL_ResetPureClientAtServer
=================
*/
void CL_ResetPureClientAtServer( void ) {
	CL_AddReliableCommand( "vdr", qfalse );
}


/*
=================
CL_Vid_Restart_f

Restart the video subsystem

we also have to reload the UI and CGame because the renderer
doesn't know what graphics to reload
=================
*/
static void CL_Vid_Restart( void ) {

	// RF, don't show percent bar, since the memory usage will just sit at the same level anyway
	com_expectedhunkusage = -1;

	// Settings may have changed so stop recording now
	if( CL_VideoRecording( ) ) {
		CL_CloseAVI( );
	}

	if(clc.demorecording)
		CL_StopRecord_f();

	// don't let them loop during the restart
	S_StopAllSounds();
	// shutdown VMs
	CL_ShutdownVMs();
	// shutdown sound system
	S_Shutdown();
	// shutdown the renderer and clear the renderer interface
	CL_ShutdownRef();
	// client is no longer pure untill new checksums are sent
	CL_ResetPureClientAtServer();
	// clear pak references
	FS_ClearPakReferences( FS_UI_REF | FS_CGAME_REF );
	// reinitialize the filesystem if the game directory or checksum has changed
	if ( !clc.demoplaying ) // -EC-
		FS_ConditionalRestart( clc.checksumFeed, qfalse );

	// ENSI - not in q3e/ioq3
	//S_BeginRegistration();  // all sound handles are now invalid

	cls.rendererStarted = qfalse;
	cls.uiStarted = qfalse;
	cls.cgameStarted = qfalse;
	cls.soundRegistered = qfalse;
	cls.soundStarted = qfalse;
	autoupdateChecked = qfalse;

	// unpause so the cgame definately gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );

	CL_ClearMemory();

	// initialize the renderer interface
	CL_InitRef();

	// startup all the client stuff
	CL_StartHunkUsers();

	// start the cgame if connected
	if ( cls.state > CA_CONNECTED && cls.state != CA_CINEMATIC ) {
		cls.cgameStarted = qtrue;
		CL_InitCGame();
		// send pure checksums
		CL_SendPureChecksums();
	}
}


/*
=================
CL_Vid_Restart_f

Wrapper for CL_Vid_Restart
=================
*/
void CL_Vid_Restart_f( void ) {

	 // hack for OSP mod: do not allow vid restart right after cgame init
	if ( cls.lastVidRestart )
		if ( abs( cls.lastVidRestart - Sys_Milliseconds() ) < 500 )
			return;

	if ( Com_DelayFunc && Com_DelayFunc != CL_Vid_Restart ) {
		Com_DPrintf( "...perform vid_restart\n" );
		CL_Vid_Restart(); // something pending, direct restart
	} else {
		Com_DPrintf( "...delay vid_restart\n" );
		Com_DelayFunc = CL_Vid_Restart; // queue restart out of rendering cycle
	}
}


/*
=================
CL_UI_Restart_f

Restart the ui subsystem
=================
*/
void CL_UI_Restart_f( void ) {          // NERVE - SMF
	// shutdown the UI
	CL_ShutdownUI();

	autoupdateChecked = qfalse;

	// init the UI
	CL_InitUI();
}

/*
=================
CL_Snd_Reload_f

Reloads sounddata from disk, retains soundhandles.
=================
*/
void CL_Snd_Reload_f( void ) {
	S_Reload();
}


/*
=================
CL_Snd_Restart

Restart the sound subsystem
=================
*/
void CL_Snd_Shutdown( void )
{
	S_StopAllSounds();
	S_Shutdown();
	cls.soundStarted = qfalse;
}


/*
=================
CL_Snd_Restart_f

Restart the sound subsystem
The cgame and game must also be forced to restart because
handles will be invalid
=================
*/
void CL_Snd_Restart_f( void ) 
{
	CL_Snd_Shutdown();
	// sound will be reinitialized by vid_restart
	CL_Vid_Restart_f();
}


/*
==================
CL_PK3List_f
==================
*/
void CL_OpenedPK3List_f( void ) {
	Com_Printf("Opened PK3 Names: %s\n", FS_LoadedPakNames());
}

/*
==================
CL_PureList_f
==================
*/
void CL_ReferencedPK3List_f( void ) {
	Com_Printf("Referenced PK3 Names: %s\n", FS_ReferencedPakNames());
}

/*
==================
CL_Configstrings_f
==================
*/
void CL_Configstrings_f( void ) {
	int		i;
	int		ofs;

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server.\n");
		return;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		ofs = cl.gameState.stringOffsets[ i ];
		if ( !ofs ) {
			continue;
		}
		Com_Printf( "%4i: %s\n", i, cl.gameState.stringData + ofs );
	}
}

/*
==============
CL_Clientinfo_f
==============
*/
void CL_Clientinfo_f( void ) {
	Com_Printf( "--------- Client Information ---------\n" );
	Com_Printf( "state: %i\n", cls.state );
	Com_Printf( "Server: %s\n", cls.servername );
	Com_Printf ("User info settings:\n");
	Info_Print( Cvar_InfoString( CVAR_USERINFO ) );
	Com_Printf( "--------------------------------------\n" );
}

/*
==============
CL_EatMe_f

Eat misc console commands to prevent exploits
==============
*/
void CL_EatMe_f( void ) {
	//do nothing kthxbye
}

//====================================================================

/*
=================
CL_DownloadsComplete

Called when all downloading has been completed
=================
*/
void CL_DownloadsComplete( void ) {

#ifndef _WIN32
	char    *fs_write_path;
#endif
	char    *fn;

	// DHM - Nerve :: Auto-update (not finished yet)
	if ( autoupdateStarted ) {

		if ( autoupdateFilename && ( strlen( autoupdateFilename ) > 4 ) ) {
#ifdef _WIN32
			// win32's Sys_StartProcess prepends the current dir
			fn = va( "%s/%s", FS_ShiftStr( AUTOUPDATE_DIR, AUTOUPDATE_DIR_SHIFT ), autoupdateFilename );
#else
			fs_write_path = Cvar_VariableString( "fs_homepath" );
			fn = FS_BuildOSPath( fs_write_path, FS_ShiftStr( AUTOUPDATE_DIR, AUTOUPDATE_DIR_SHIFT ), autoupdateFilename );
#ifndef __MACOS__
			Sys_Chmod( fn, S_IXUSR );
#endif
#endif
			// will either exit with a successful process spawn, or will Com_Error ERR_DROP
			// so we need to clear the disconnected download data if needed
			if ( cls.bWWWDlDisconnected ) {
				cls.bWWWDlDisconnected = qfalse;
				CL_ClearStaticDownload();
			}
			Sys_StartProcess( fn, qtrue );
		}

		// NOTE - TTimo: that code is never supposed to be reached?

		autoupdateStarted = qfalse;

		if ( !cls.bWWWDlDisconnected ) {
			CL_Disconnect( qtrue );
		}
		// we can reset that now
		cls.bWWWDlDisconnected = qfalse;
		CL_ClearStaticDownload();

		return;
	}

	// if we downloaded files we need to restart the file system
	if ( cls.downloadRestart ) {
		cls.downloadRestart = qfalse;

		FS_Restart( clc.checksumFeed ); // We possibly downloaded a pak, restart the file system to load it

		if ( !cls.bWWWDlDisconnected ) {
			// inform the server so we get new gamestate info
			CL_AddReliableCommand( "donedl", qfalse );
		}
		// we can reset that now
		cls.bWWWDlDisconnected = qfalse;
		CL_ClearStaticDownload();

		// by sending the donedl command we request a new gamestate
		// so we don't want to load stuff yet
		return;
	}

	// TTimo: I wonder if that happens - it should not but I suspect it could happen if a download fails in the middle or is aborted
	assert( !cls.bWWWDlDisconnected );

	// let the client game init and load data
	cls.state = CA_LOADING;

	// Pump the loop, this may change gamestate!
	Com_EventLoop();

	// if the gamestate was changed by calling Com_EventLoop
	// then we loaded everything already and we don't want to do it again.
	if ( cls.state != CA_LOADING ) {
		return;
	}

	// starting to load a map so we get out of full screen ui mode
	Cvar_Set( "r_uiFullScreen", "0" );

	// flush client memory and start loading stuff
	// this will also (re)load the UI
	// if this is a local client then only the client part of the hunk
	// will be cleared, note that this is done after the hunk mark has been set
	CL_FlushMemory();

	// initialize the CGame
	cls.cgameStarted = qtrue;
	CL_InitCGame();

	// set pure checksums
	CL_SendPureChecksums();

	CL_WritePacket();
	CL_WritePacket();
	CL_WritePacket();
}


/*
=================
CL_BeginDownload

Requests a file to download from the server.  Stores it in the current
game directory.
=================
*/
void CL_BeginDownload( const char *localName, const char *remoteName ) {

	Com_DPrintf("***** CL_BeginDownload *****\n"
				"Localname: %s\n"
				"Remotename: %s\n"
				"****************************\n", localName, remoteName);

	Q_strncpyz ( clc.downloadName, localName, sizeof(clc.downloadName) );
	Com_sprintf( clc.downloadTempName, sizeof(clc.downloadTempName), "%s.tmp", localName );

	// Set so UI gets access to it
	Cvar_Set( "cl_downloadName", remoteName );
	Cvar_Set( "cl_downloadSize", "0" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_SetValue( "cl_downloadTime", cls.realtime );

	clc.downloadBlock = 0; // Starting new file
	clc.downloadCount = 0;

	CL_AddReliableCommand( va("download %s", remoteName), qfalse );
}


/*
=================
CL_NextDownload

A download completed or failed
=================
*/
void CL_NextDownload( void ) {
	char *s;
	char *remoteName, *localName;

 	// A download has finished, check whether this matches a referenced checksum
 	if(*clc.downloadName)
 	{
 		const char *zippath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), clc.downloadName, NULL );

 		if(!FS_CompareZipChecksum(zippath))
 			Com_Error(ERR_DROP, "Incorrect checksum for file: %s", clc.downloadName);
 	}

 	*clc.downloadTempName = *clc.downloadName = 0;
 	Cvar_Set("cl_downloadName", "");

	// We are looking to start a download here
	if (*clc.downloadList) {
		s = clc.downloadList;

		// format is:
		//  @remotename@localname@remotename@localname, etc.

		if (*s == '@')
			s++;
		remoteName = s;
		
		if ( (s = strchr(s, '@')) == NULL ) {
			CL_DownloadsComplete();
			return;
		}

		*s++ = 0;
		localName = s;
		if ( (s = strchr(s, '@')) != NULL )
			*s++ = 0;
		else
			s = localName + strlen(localName); // point at the null byte


		CL_BeginDownload( localName, remoteName );

		clc.downloadRestart = qtrue;

		// move over the rest
		memmove( clc.downloadList, s, strlen(s) + 1 );

		return;
	}

	CL_DownloadsComplete();
}


/*
=================
CL_InitDownloads

After receiving a valid game state, we valid the cgame and local zip files here
and determine if we need to download them
=================
*/
void CL_InitDownloads( void ) {
#ifndef PRE_RELEASE_DEMO
	char missingfiles[1024];
	char *dir = FS_ShiftStr( AUTOUPDATE_DIR, AUTOUPDATE_DIR_SHIFT );

	// TTimo
	// init some of the www dl data
	clc.bWWWDl = qfalse;
	clc.bWWWDlAborting = qfalse;
	cls.bWWWDlDisconnected = qfalse;
	CL_ClearStaticDownload();

	if ( autoupdateStarted && NET_CompareAdr( &cls.autoupdateServer, &clc.serverAddress ) ) {
		if ( strlen( cl_updatefiles->string ) > 4 ) {
			Q_strncpyz( autoupdateFilename, cl_updatefiles->string, sizeof( autoupdateFilename ) );
			Q_strncpyz( clc.downloadList, va( "@%s/%s@%s/%s", dir, cl_updatefiles->string, dir, cl_updatefiles->string ), MAX_INFO_STRING );
			cls.state = CA_CONNECTED;
			CL_NextDownload();
			return;
		}
	} else
	{
		// whatever autodownlad configuration, store missing files in a cvar, use later in the ui maybe
		if ( FS_ComparePaks( missingfiles, sizeof( missingfiles ), qfalse ) ) {
			Cvar_Set( "com_missingFiles", missingfiles );
		} else {
			Cvar_Set( "com_missingFiles", "" );
		}

		// reset the redirect checksum tracking
		clc.redirectedList[0] = '\0';

		if ( cl_allowDownload->integer && FS_ComparePaks( clc.downloadList, sizeof( clc.downloadList ), qtrue ) ) {
			// this gets printed to UI, i18n
			Com_Printf( CL_TranslateStringBuf( "Need paks: %s\n" ), clc.downloadList );

			if ( *clc.downloadList ) {
				// if autodownloading is not enabled on the server
				cls.state = CA_CONNECTED;
				CL_NextDownload();
				return;
			}
		}
	}
#endif

	CL_DownloadsComplete();
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
static void CL_CheckForResend( void ) {
	int		port, len;
	char	info[MAX_INFO_STRING];
	char	data[MAX_INFO_STRING + 10];

	// don't send anything if playing back a demo
	if ( clc.demoplaying ) {
		return;
	}

	// resend if we haven't gotten a reply yet
	if ( cls.state != CA_CONNECTING && cls.state != CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;

	switch ( cls.state ) {
	case CA_CONNECTING:
		// requesting a challenge .. IPv6 users always get in as authorize server supports no ipv6.
#ifdef AUTHORIZE_SUPPORT
		if ( clc.serverAddress.type == NA_IP && !Sys_IsLANAddress( &clc.serverAddress ) )
			CL_RequestAuthorization();
#endif // AUTHORIZE_SUPPORT
		// The challenge request shall be followed by a client challenge so no malicious server can hijack this connection.
		NET_OutOfBandPrint( NS_CLIENT, &clc.serverAddress, "getchallenge %d", clc.challenge );
		break;

	case CA_CHALLENGING:
		// sending back the challenge
		port = Cvar_VariableIntegerValue( "net_qport" );

		Q_strncpyz( info, Cvar_InfoString( CVAR_USERINFO ), sizeof( info ) );
		
		// if(com_legacyprotocol->integer == com_protocol->integer)
		//	clc.compat = qtrue;

		if ( clc.compat )
			Info_SetValueForKey(info, "protocol", va("%i", PROTOCOL_VERSION ));
		else
			Info_SetValueForKey(info, "protocol", va("%i", NEW_PROTOCOL_VERSION ));

		Info_SetValueForKey( info, "qport", va( "%i", port ) );
		Info_SetValueForKey( info, "challenge", va( "%i", clc.challenge ) );

		len = Com_sprintf( data, sizeof( data ), "connect \"%s\"", info );
		// NOTE TTimo don't forget to set the right data length!
		NET_OutOfBandData( NS_CLIENT, &clc.serverAddress, (byte *) &data[0], len );
		// the most current userinfo has been sent, so watch for any
		// newer changes to userinfo variables
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad cls.state" );
	}
}

/*
===================
CL_DisconnectPacket

Sometimes the server can drop the client and the netchan based
disconnect can be lost.  If the client continues to send packets
to the server, the server will send out of band disconnect packets
to the client so it doesn't have to wait for the full timeout period.
===================
*/
void CL_DisconnectPacket( const netadr_t *from ) {
	const char* message;

	if ( cls.state < CA_AUTHORIZING ) {
		return;
	}

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, &clc.netchan.remoteAddress ) ) {
		return;
	}

	// if we have received packets within three seconds, ignore (it might be a malicious spoof)
	// NOTE TTimo:
	// there used to be a  clc.lastPacketTime = cls.realtime; line in CL_PacketEvent before calling CL_ConnectionLessPacket
	// therefore .. packets never got through this check, clients never disconnected
	// switched the clc.lastPacketTime = cls.realtime to happen after the connectionless packets have been processed
	// you still can't spoof disconnects, cause legal netchan packets will maintain realtime - lastPacketTime below the threshold
	if ( cls.realtime - clc.lastPacketTime < 3000 ) {
		return;
	}

	// if we are doing a disconnected download, leave the 'connecting' screen on with the progress information
	if ( !cls.bWWWDlDisconnected ) {
		// drop the connection
		message = "Server disconnected for unknown reason";
		Com_Printf( message );
		Cvar_Set( "com_errorMessage", message );
		CL_Disconnect( qtrue );
	} else {
		CL_Disconnect( qfalse );
		Cvar_Set( "ui_connecting", "1" );
		Cvar_Set( "ui_dl_running", "1" );
	}
}


/*
===================
CL_MotdPacket
===================
*/
static void CL_MotdPacket( const netadr_t *from ) {
	char	*challenge;
	char	*info;

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, &cls.updateServer ) ) {
		return;
	}

	info = Cmd_Argv(1);

	// check challenge
	challenge = Info_ValueForKey( info, "challenge" );
	if ( strcmp( challenge, cls.updateChallenge ) ) {
		return;
	}

	challenge = Info_ValueForKey( info, "motd" );

	Q_strncpyz( cls.updateInfoString, info, sizeof( cls.updateInfoString ) );
	Cvar_Set( "cl_motdString", challenge );
}

/*
===================
CL_PrintPackets
an OOB message from server, with potential markups
print OOB are the only messages we handle markups in
[err_dialog]: used to indicate that the connection should be aborted
  no further information, just do an error diagnostic screen afterwards
[err_prot]: HACK. This is a protocol error. The client uses a custom
  protocol error message (client sided) in the diagnostic window.
  The space for the error message on the connection screen is limited
  to 256 chars.
===================
*/
static void CL_PrintPacket( const netadr_t *from, msg_t *msg ) {
	const char *s = MSG_ReadBigString( msg );

	if ( !Q_stricmpn( s, "[err_dialog]", 12 ) ) {
		Q_strncpyz( clc.serverMessage, s + 12, sizeof( clc.serverMessage ) );
		// Cvar_Set("com_errorMessage", clc.serverMessage );
		Com_Error( ERR_DROP, clc.serverMessage );
	} else if ( !Q_stricmpn( s, "[err_prot]", 10 ) )       {
		Q_strncpyz( clc.serverMessage, s + 10, sizeof( clc.serverMessage ) );
		// Cvar_Set("com_errorMessage", CL_TranslateStringBuf( PROTOCOL_MISMATCH_ERROR_LONG ) );
		Com_Error( ERR_DROP, CL_TranslateStringBuf( PROTOCOL_MISMATCH_ERROR_LONG ) );
	} else if ( !Q_stricmpn( s, "[err_update]", 12 ) )       {
		Q_strncpyz( clc.serverMessage, s + 12, sizeof( clc.serverMessage ) );
		Com_Error( ERR_AUTOUPDATE, clc.serverMessage );
	} else if ( !Q_stricmpn( s, "ET://", 5 ) )       { // fretn
		Q_strncpyz( clc.serverMessage, s, sizeof( clc.serverMessage ) );
		Cvar_Set( "com_errorMessage", clc.serverMessage );
		Com_Error( ERR_DROP, clc.serverMessage );
	} else {
		Q_strncpyz( clc.serverMessage, s, sizeof( clc.serverMessage ) );
	}
	Com_Printf( "%s", clc.serverMessage );
}

/*
===================
CL_InitServerInfo
===================
*/
static void CL_InitServerInfo( serverInfo_t *server, const netadr_t *address ) {
	server->adr = *address;
	server->clients = 0;
	server->hostName[0] = '\0';
	server->mapName[0] = '\0';
	server->maxClients = 0;
	server->maxPing = 0;
	server->minPing = 0;
	server->ping = -1;
	server->game[0] = '\0';
	server->gameType = 0;
	server->netType = 0;
	server->allowAnonymous = 0;

	//cls.localServers[i].clients = 0;
	//cls.localServers[i].hostName[0] = '\0';
	//cls.localServers[i].load = -1;
	//cls.localServers[i].mapName[0] = '\0';
	//cls.localServers[i].maxClients = 0;
	//cls.localServers[i].maxPing = 0;
	//cls.localServers[i].minPing = 0;
	//cls.localServers[i].ping = -1;
	//cls.localServers[i].game[0] = '\0';
	//cls.localServers[i].gameType = 0;
	//cls.localServers[i].netType = from.type;
	//cls.localServers[i].allowAnonymous = 0;
	//cls.localServers[i].friendlyFire = 0;           // NERVE - SMF
	//cls.localServers[i].maxlives = 0;               // NERVE - SMF
	//cls.localServers[i].needpass = 0;
	//cls.localServers[i].punkbuster = 0;             // DHM - Nerve
	//cls.localServers[i].antilag = 0;
	//cls.localServers[i].weaprestrict = 0;
	//cls.localServers[i].balancedteams = 0;
	//cls.localServers[i].gameName[0] = '\0';           // Arnout
}

#define MAX_SERVERSPERPACKET    256

typedef struct hash_chain_s {
	netadr_t             addr;
	struct hash_chain_s *next;
} hash_chain_t;

hash_chain_t *hash_table[1024]; 
hash_chain_t hash_list[MAX_GLOBAL_SERVERS];
unsigned int hash_count = 0;

unsigned int hash_func( const netadr_t *addr ) {

	const byte		*ip = NULL;
	unsigned int	size;
	unsigned int	i;
	unsigned int	hash = 0;

	switch ( addr->type ) {
		case NA_IP:  ip = addr->ip;  size = 4; break;
		case NA_IP6: ip = addr->ip6; size = 16; break;
		default: size = 0; break;
	}

	for ( i = 0; i < size; i++ )
		hash = hash * 101 + (int)( *ip++ );

	hash = hash ^ ( hash >> 16 );

	return (hash & 1023);
}

static void hash_insert( const netadr_t *addr ) 
{
	hash_chain_t **tab, *cur;
	unsigned int hash;
	if ( hash_count >= MAX_GLOBAL_SERVERS )
		return;
	hash = hash_func( addr );
	tab = &hash_table[ hash ];
	cur = &hash_list[ hash_count++ ];
	cur->addr = *addr;
	if ( cur != *tab )
		cur->next = *tab;
	else
		cur->next = NULL;
	*tab = cur;
}

static void hash_reset( void ) 
{
	hash_count = 0;
	memset( hash_list, 0, sizeof( hash_list ) );
	memset( hash_table, 0, sizeof( hash_table ) );
}

static hash_chain_t *hash_find( const netadr_t *addr )
{
	hash_chain_t *cur;
	cur = hash_table[ hash_func( addr ) ];
	while ( cur != NULL ) {
		if ( NET_CompareAdr( addr, &cur->addr ) )
			return cur;
		cur = cur->next;
	}
	return NULL;
}


/*
===================
CL_ServersResponsePacket
===================
*/
static void CL_ServersResponsePacket( const netadr_t* from, msg_t *msg, qboolean extended ) {
	int				i, count, total;
	netadr_t addresses[MAX_SERVERSPERPACKET];
	int				numservers;
	byte*			buffptr;
	byte*			buffend;
	serverInfo_t	*server;
	
	//Com_Printf("CL_ServersResponsePacket\n"); // moved down

	if (cls.numglobalservers == -1) {
		// state to detect lack of servers or lack of response
		cls.numglobalservers = 0;
		cls.numGlobalServerAddresses = 0;
		hash_reset();
	}

	// parse through server response string
	numservers = 0;
	buffptr    = msg->data;
	buffend    = buffptr + msg->cursize;

	// advance to initial token
	do
	{
		if(*buffptr == '\\' || (extended && *buffptr == '/'))
			break;
		
		buffptr++;
	} while (buffptr < buffend);

	while (buffptr + 1 < buffend)
	{
		// IPv4 address
		if (*buffptr == '\\')
		{
			buffptr++;

			if (buffend - buffptr < sizeof(addresses[numservers].ip) + sizeof(addresses[numservers].port) + 1)
				break;

			for(i = 0; i < sizeof(addresses[numservers].ip); i++)
				addresses[numservers].ip[i] = *buffptr++;

			addresses[numservers].type = NA_IP;
		}
		// IPv6 address, if it's an extended response
		else if (extended && *buffptr == '/')
		{
			buffptr++;

			if (buffend - buffptr < sizeof(addresses[numservers].ip6) + sizeof(addresses[numservers].port) + 1)
				break;
			
			for(i = 0; i < sizeof(addresses[numservers].ip6); i++)
				addresses[numservers].ip6[i] = *buffptr++;
			
			addresses[numservers].type = NA_IP6;
			addresses[numservers].scope_id = from->scope_id;
		}
		else
			// syntax error!
			break;
			
		// parse out port
		addresses[numservers].port = (*buffptr++) << 8;
		addresses[numservers].port += *buffptr++;
		addresses[numservers].port = BigShort( addresses[numservers].port );

		// syntax check
		if (*buffptr != '\\' && *buffptr != '/')
			break;
	
		numservers++;
		if (numservers >= MAX_SERVERSPERPACKET)
			break;

		// parse out EOT
		if ( buffptr[1] == 'E' && buffptr[2] == 'O' && buffptr[3] == 'T' ) {
			break;
		}
	}

	count = cls.numglobalservers;

	for (i = 0; i < numservers && count < MAX_GLOBAL_SERVERS; i++) {

		// Tequila: It's possible to have sent many master server requests. Then
		// we may receive many times the same addresses from the master server.
		// We just avoid to add a server if it is still in the global servers list.
		if ( hash_find( &addresses[i] ) )
			continue;

		hash_insert( &addresses[i] );

		// build net address
		server = &cls.globalServers[count];

		CL_InitServerInfo( server, &addresses[i] );
		// advance to next slot
		count++;
	}

	// if getting the global list
	if ( count >= MAX_GLOBAL_SERVERS && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS )
	{
		// if we couldn't store the servers in the main list anymore
		for (; i < numservers && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS; i++)
		{
			// just store the addresses in an additional list
			cls.globalServerAddresses[cls.numGlobalServerAddresses++] = addresses[i];
		}
	}

	cls.numglobalservers = count;
	total = count + cls.numGlobalServerAddresses;

	Com_Printf( "getserversResponse:%3d servers parsed (total %d)\n", numservers, total);
}


/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
static void CL_ConnectionlessPacket( const netadr_t *from, msg_t *msg ) {
	const char *s;
	char	*c;
	int challenge = 0;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);

	if ( com_developer->integer ) {
		Com_Printf( "CL packet %s: %s\n", NET_AdrToStringwPort( from ), s );
	}

	// challenge from the server we are connecting to
	if (!Q_stricmp(c, "challengeResponse"))
	{
		char *strver;
		int ver;
	
		if ( cls.state != CA_CONNECTING )
		{
			Com_DPrintf("Unwanted challenge response received. Ignored.\n");
			return;
		}
		
		c = Cmd_Argv( 3 );
		if ( *c )
			challenge = atoi( c );

 		clc.compat = qtrue;
		strver = Cmd_Argv( 4 ); // analyze server protocol version
		if ( *strver ) {
			ver = atoi( strver );
			if ( ver != PROTOCOL_VERSION ) {
				if ( ver == NEW_PROTOCOL_VERSION ) {
					clc.compat = qfalse;
				} else {
					Com_Printf( S_COLOR_YELLOW "Warning: Server reports protocol version %d, "
						   "we have %d. Trying legacy protocol %d.\n",
						   ver, NEW_PROTOCOL_VERSION, PROTOCOL_VERSION );
				}
			}
		}
		
		if ( clc.compat )
		{
			if( !NET_CompareAdr( from, &clc.serverAddress ) )
			{
				// This challenge response is not coming from the expected address.
				// Check whether we have a matching client challenge to prevent
				// connection hi-jacking.
				if( !*c || challenge != clc.challenge )
				{
					Com_DPrintf( "Challenge response received from unexpected source. Ignored.\n" );
					return;
				}
			}
		}
		else
		{
			if( !*c || challenge != clc.challenge )
			{
				Com_Printf("Bad challenge for challengeResponse. Ignored.\n");
				return;
			}
		}

		if ( Cmd_Argc() > 2 ) {
			clc.onlyVisibleClients = atoi( Cmd_Argv( 2 ) );         // DHM - Nerve
		} else {
			clc.onlyVisibleClients = 0;
		}

		// start sending challenge response instead of challenge request packets
		clc.challenge = atoi(Cmd_Argv(1));
		cls.state = CA_CHALLENGING;
		clc.connectPacketCount = 0;
		clc.connectTime = -99999;

		// take this address as the new server address.  This allows
		// a server proxy to hand off connections to multiple servers
		clc.serverAddress = *from;
		Com_DPrintf ("challengeResponse: %d\n", clc.challenge);
		return;
	}

	// server connection
	if ( !Q_stricmp(c, "connectResponse") ) {
		if ( cls.state >= CA_CONNECTED ) {
			Com_Printf ("Dup connect received.  Ignored.\n");
			return;
		}
		if ( cls.state != CA_CHALLENGING ) {
			Com_Printf ("connectResponse packet while not connecting. Ignored.\n");
			return;
		}
		if ( !NET_CompareAdr( from, &clc.serverAddress ) ) {
			Com_Printf( "connectResponse from wrong address. Ignored.\n" );
			return;
		}

		if ( !clc.compat )
		{
			c = Cmd_Argv(1);

			if(*c)
				challenge = atoi(c);
			else
			{
				Com_Printf("Bad connectResponse received. Ignored.\n");
				return;
			}
			
			if(challenge != clc.challenge)
			{
				Com_Printf("ConnectResponse with bad challenge received. Ignored.\n");
				return;
			}
		}

		Netchan_Setup( NS_CLIENT, &clc.netchan, from, Cvar_VariableIntegerValue("net_qport"),
			clc.challenge, clc.compat );

		cls.state = CA_CONNECTED;
		clc.lastPacketSentTime = -9999;		// send first packet immediately
		return;
	}

	// server responding to an info broadcast
	if ( !Q_stricmp(c, "infoResponse") ) {
		CL_ServerInfoPacket( from, msg );
		return;
	}

	// server responding to a get playerlist
	if ( !Q_stricmp(c, "statusResponse") ) {
		CL_ServerStatusResponse( from, msg );
		return;
	}

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but it is still getting packets from us
	if ( !Q_stricmp( c, "disconnect" ) ) {
		CL_DisconnectPacket( from );
		return;
	}

	// echo request from server
	if ( !Q_stricmp(c, "echo") ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv(1) );
		return;
	}

	// cd check
	if ( !Q_stricmp(c, "keyAuthorize") ) {
		// we don't use these now, so dump them on the floor
		return;
	}

	// global MOTD from id
	if ( !Q_stricmp(c, "motd") ) {
		CL_MotdPacket( from );
		return;
	}

	// echo request from server
	if ( !Q_stricmp(c, "print") ) {
		CL_PrintPacket( from, msg );
		return;
	}

	// DHM - Nerve :: Auto-update server response message
	//if ( !Q_stricmp( c, "updateResponse" ) ) {
//		CL_UpdateInfoPacket( from );
//		return;
//	}
	// DHM - Nerve

	// NERVE - SMF - bugfix, make this compare first n chars so it doesnt bail if token is parsed incorrectly
	// list of servers sent back by a master server (classic)
	if ( !Q_strncmp(c, "getserversResponse", 18) ) {
		CL_ServersResponsePacket( from, msg, qfalse );
		return;
	}

	// list of servers sent back by a master server (extended)
	if ( !Q_strncmp(c, "getserversExtResponse", 21) ) {
		CL_ServersResponsePacket( from, msg, qtrue );
		return;
	}

	Com_DPrintf( "Unknown connectionless packet command.\n" );
}


/*
=================
CL_PacketEvent

A packet has arrived from the main event loop
=================
*/
void CL_PacketEvent( const netadr_t *from, msg_t *msg ) {
	int		headerBytes;

	clc.lastPacketTime = cls.realtime; // -EC- FIXME: move down?

	if ( msg->cursize >= 4 && *(int *)msg->data == -1 ) {
		CL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( cls.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg->cursize < 4 ) {
		Com_Printf ("%s: Runt packet\n", NET_AdrToStringwPort( from ));
		return;
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, &clc.netchan.remoteAddress ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "%s:sequenced packet without connection\n",
				NET_AdrToStringwPort( from ) );
		}
		// FIXME: send a client disconnect?
		return;
	}

	if ( !CL_Netchan_Process( &clc.netchan, msg ) ) {
		return;		// out of order, duplicated, etc
	}

	// the header is different lengths for reliable and unreliable messages
	headerBytes = msg->readcount;

	// track the last message received so it can be returned in 
	// client messages, allowing the server to detect a dropped
	// gamestate
	clc.serverMessageSequence = LittleLong( *(int *)msg->data );

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );

	//
	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	//
	if ( clc.demorecording && !clc.demowaiting && !clc.demoplaying ) {
		CL_WriteDemoMessage( msg, headerBytes );
	}
}

/*
==================
CL_CheckTimeout

==================
*/
void CL_CheckTimeout( void ) {
	//
	// check timeout
	//
	if ( ( !CL_CheckPaused() || !sv_paused->integer ) 
		&& cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC
	    && cls.realtime - clc.lastPacketTime > cl_timeout->value*1000) {
		if (++cl.timeoutcount > 5) {	// timeoutcount saves debugger
			Cvar_Set( "com_errorMessage", "Server connection timed out." );
			CL_Disconnect( qtrue );
			return;
		}
	} else {
		cl.timeoutcount = 0;
	}
}

/*
==================
CL_CheckPaused
Check whether client has been paused.
==================
*/
qboolean CL_CheckPaused(void)
{
	// if cl_paused->modified is set, the cvar has only been changed in
	// this frame. Keep paused in this frame to ensure the server doesn't
	// lag behind.
	if(cl_paused->integer || cl_paused->modified)
		return qtrue;
	
	return qfalse;
}

//============================================================================

/*
==================
CL_CheckUserinfo

==================
*/
void CL_CheckUserinfo( void ) {

	// don't add reliable commands when not yet connected
	if( cls.state < CA_CONNECTED )
		return;

	// don't overflow the reliable command buffer when paused
	if( CL_CheckPaused() )
		return;

	// send a reliable userinfo update if needed
	if( cvar_modifiedFlags & CVAR_USERINFO )
	{
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		CL_AddReliableCommand( va("userinfo \"%s\"", Cvar_InfoString( CVAR_USERINFO ) ), qfalse );
	}
}

/*
==================
CL_WWWDownload
==================
*/
void CL_WWWDownload( void ) {
	char *to_ospath;
	dlStatus_t ret;
	static qboolean bAbort = qfalse;

	if ( clc.bWWWDlAborting ) {
		if ( !bAbort ) {
			Com_DPrintf( "CL_WWWDownload: WWWDlAborting\n" );
			bAbort = qtrue;
		}
		return;
	}
	if ( bAbort ) {
		Com_DPrintf( "CL_WWWDownload: WWWDlAborting done\n" );
		bAbort = qfalse;
	}

	ret = DL_DownloadLoop();

	if ( ret == DL_CONTINUE ) {
		return;
	}

	if ( ret == DL_DONE ) {
		// taken from CL_ParseDownload
		// we work with OS paths
		clc.download = 0;
		to_ospath = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), cls.originalDownloadName, "" );
		to_ospath[strlen( to_ospath ) - 1] = '\0';
		if ( rename( cls.downloadTempName, to_ospath ) ) {
			FS_CopyFile( cls.downloadTempName, to_ospath );
			remove( cls.downloadTempName );
		}
		*cls.downloadTempName = *cls.downloadName = 0;
		Cvar_Set( "cl_downloadName", "" );
		if ( cls.bWWWDlDisconnected ) {
			// for an auto-update in disconnected mode, we'll be spawning the setup in CL_DownloadsComplete
			if ( !autoupdateStarted ) {
				// reconnect to the server, which might send us to a new disconnected download
				Cbuf_ExecuteText( EXEC_APPEND, "reconnect\n" );
			}
		} else {
			CL_AddReliableCommand( "wwwdl done", qfalse );
			// tracking potential web redirects leading us to wrong checksum - only works in connected mode
			if ( strlen( clc.redirectedList ) + strlen( cls.originalDownloadName ) + 1 >= sizeof( clc.redirectedList ) ) {
				// just to be safe
				Com_Printf( "ERROR: redirectedList overflow (%s)\n", clc.redirectedList );
			} else {
				strcat( clc.redirectedList, "@" );
				strcat( clc.redirectedList, cls.originalDownloadName );
			}
		}
	} else
	{
		if ( cls.bWWWDlDisconnected ) {
			// in a connected download, we'd tell the server about failure and wait for a reply
			// but in this case we can't get anything from server
			// if we just reconnect it's likely we'll get the same disconnected download message, and error out again
			// this may happen for a regular dl or an auto update
			const char *error = va( "Download failure while getting '%s'\n", cls.downloadName ); // get the msg before clearing structs
			cls.bWWWDlDisconnected = qfalse; // need clearing structs before ERR_DROP, or it goes into endless reload
			CL_ClearStaticDownload();
			Com_Error( ERR_DROP, error );
		} else {
			// see CL_ParseDownload, same abort strategy
			Com_Printf( "Download failure while getting '%s'\n", cls.downloadName );
			CL_AddReliableCommand( "wwwdl fail", qfalse );
			clc.bWWWDlAborting = qtrue;
		}
		return;
	}

	clc.bWWWDl = qfalse;
	CL_NextDownload();
}

/*
==================
CL_WWWBadChecksum

FS code calls this when doing FS_ComparePaks
we can detect files that we got from a www dl redirect with a wrong checksum
this indicates that the redirect setup is broken, and next dl attempt should NOT redirect
==================
*/
qboolean CL_WWWBadChecksum( const char *pakname ) {
	if ( strstr( clc.redirectedList, va( "@%s", pakname ) ) ) {
		Com_Printf( "WARNING: file %s obtained through download redirect has wrong checksum\n", pakname );
		Com_Printf( "         this likely means the server configuration is broken\n" );
		if ( strlen( clc.badChecksumList ) + strlen( pakname ) + 1 >= sizeof( clc.badChecksumList ) ) {
			Com_Printf( "ERROR: badChecksumList overflowed (%s)\n", clc.badChecksumList );
			return qfalse;
		}
		strcat( clc.badChecksumList, "@" );
		strcat( clc.badChecksumList, pakname );
		Com_DPrintf( "bad checksums: %s\n", clc.badChecksumList );
		return qtrue;
	}
	return qfalse;
}

/*
==================
CL_Frame

==================
*/
//#ifdef USE_PMLIGHT
extern cvar_t *r_dlightSpecPower;
extern cvar_t *r_dlightSpecColor;
extern qboolean ARB_UpdatePrograms( void );
//#endif
void CL_Frame( int msec ) {
	float fps;
	float frameDuration;

	if ( !com_cl_running->integer ) {
		return;
	}

	if ( cls.cddialog ) {
		// bring up the cd error dialog if needed
		cls.cddialog = qfalse;
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NEED_CD );
	} else	if ( cls.state == CA_DISCONNECTED && !( Key_GetCatcher( ) & KEYCATCH_UI )
		&& !com_sv_running->integer && uivm ) {
		// if disconnected, bring up the menu
		S_StopAllSounds();
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
	}

	// if recording an avi, lock to a fixed fps
	if ( CL_VideoRecording() && cl_aviFrameRate->integer && msec ) {
		// save the current screen
		if ( cls.state == CA_ACTIVE || cl_forceavidemo->integer ) {

			if ( com_timescale->value > 0.0001f )
				fps = MIN( cl_aviFrameRate->value / com_timescale->value, 1000.0f );
			else
				fps = 1000.0f;

			frameDuration = MAX( 1000.0f / fps, 1.0f ) + clc.aviVideoFrameRemainder;

			CL_TakeVideoFrame();

			msec = (int)frameDuration;
			clc.aviVideoFrameRemainder = frameDuration - msec;
		}
	}
	
	if ( cl_autoRecordDemo->integer && !clc.demoplaying ) {
		if ( cls.state == CA_ACTIVE && !clc.demorecording ) {
			// If not recording a demo, and we should be, start one
			qtime_t	now;
			const char	*nowString;
			char		*p;
			char		mapName[ MAX_QPATH ];
			char		serverName[ MAX_OSPATH ];

			Com_RealTime( &now );
			nowString = va( "%04d%02d%02d%02d%02d%02d",
					1900 + now.tm_year,
					1 + now.tm_mon,
					now.tm_mday,
					now.tm_hour,
					now.tm_min,
					now.tm_sec );

			Q_strncpyz( serverName, cls.servername, MAX_OSPATH );
			// Replace the ":" in the address as it is not a valid
			// file name character
			p = strchr( serverName, ':' );
			if( p ) {
				*p = '.';
			}

			Q_strncpyz( mapName, COM_SkipPath( cl.mapname ), sizeof( cl.mapname ) );
			COM_StripExtension(mapName, mapName, sizeof(mapName));

			Cbuf_ExecuteText( EXEC_NOW,
					va( "record %s-%s-%s", nowString, serverName, mapName ) );
		}
		else if( cls.state != CA_ACTIVE && clc.demorecording ) {
			// Recording, but not CA_ACTIVE, so stop recording
			CL_StopRecord_f( );
		}
	}

	// save the msec before checking pause
	cls.realFrametime = msec;

	// decide the simulation time
	cls.frametime = msec;

	cls.realtime += cls.frametime;

	if ( cl_timegraph->integer ) {
		SCR_DebugGraph( cls.realFrametime * 0.25, 0 );
	}

	// see if we need to update any userinfo
	CL_CheckUserinfo();

	// if we haven't gotten a packet in a long time,
	// drop the connection
	CL_CheckTimeout();

	// wwwdl download may survive a server disconnect
	if ( ( cls.state == CA_CONNECTED && clc.bWWWDl ) || cls.bWWWDlDisconnected ) {
		CL_WWWDownload();
	}

	// send intentions now
	CL_SendCmd();

	// resend a connection request if necessary
	CL_CheckForResend();

	// decide on the serverTime to render
	CL_SetCGameTime();

	// update the screen
	SCR_UpdateScreen();

	// update audio
	S_Update();

	// advance local effects for next frame
	SCR_RunCinematic();

	Con_RunConsole();
	//#ifdef USE_PMLIGHT
	if ( r_dlightSpecPower->modified || r_dlightSpecColor->modified ) {
		ARB_UpdatePrograms();
	}
	//#endif
	cls.framecount++;
}


//============================================================================
// Ridah, startup-caching system
typedef struct {
	char name[MAX_QPATH];
	int hits;
	int lastSetIndex;
} cacheItem_t;
typedef enum {
	CACHE_SOUNDS,
	CACHE_MODELS,
	CACHE_IMAGES,

	CACHE_NUMGROUPS
} cacheGroup_t;
static cacheItem_t cacheGroups[CACHE_NUMGROUPS] = {
	{{'s','o','u','n','d',0}, CACHE_SOUNDS},
	{{'m','o','d','e','l',0}, CACHE_MODELS},
	{{'i','m','a','g','e',0}, CACHE_IMAGES},
};
#define MAX_CACHE_ITEMS     4096
#define CACHE_HIT_RATIO     0.75        // if hit on this percentage of maps, it'll get cached

static int cacheIndex;
static cacheItem_t cacheItems[CACHE_NUMGROUPS][MAX_CACHE_ITEMS];

static void CL_Cache_StartGather_f( void ) {
	cacheIndex = 0;
	memset( cacheItems, 0, sizeof( cacheItems ) );

	Cvar_Set( "cl_cacheGathering", "1" );
}

static void CL_Cache_UsedFile_f( void ) {
	char groupStr[MAX_QPATH];
	char itemStr[MAX_QPATH];
	int i,group;
	cacheItem_t *item;

	if ( Cmd_Argc() < 2 ) {
		Com_Error( ERR_DROP, "usedfile without enough parameters\n" );
		return;
	}

	strcpy( groupStr, Cmd_Argv( 1 ) );

	strcpy( itemStr, Cmd_Argv( 2 ) );
	for ( i = 3; i < Cmd_Argc(); i++ ) {
		strcat( itemStr, " " );
		strcat( itemStr, Cmd_Argv( i ) );
	}
	Q_strlwr( itemStr );

	// find the cache group
	for ( i = 0; i < CACHE_NUMGROUPS; i++ ) {
		if ( !Q_strncmp( groupStr, cacheGroups[i].name, MAX_QPATH ) ) {
			break;
		}
	}
	if ( i == CACHE_NUMGROUPS ) {
		Com_Error( ERR_DROP, "usedfile without a valid cache group\n" );
		return;
	}

	// see if it's already there
	group = i;
	for ( i = 0, item = cacheItems[group]; i < MAX_CACHE_ITEMS; i++, item++ ) {
		if ( !item->name[0] ) {
			// didn't find it, so add it here
			Q_strncpyz( item->name, itemStr, MAX_QPATH );
			if ( cacheIndex > 9999 ) { // hack, but yeh
				item->hits = cacheIndex;
			} else {
				item->hits++;
			}
			item->lastSetIndex = cacheIndex;
			break;
		}
		if ( item->name[0] == itemStr[0] && !Q_strncmp( item->name, itemStr, MAX_QPATH ) ) {
			if ( item->lastSetIndex != cacheIndex ) {
				item->hits++;
				item->lastSetIndex = cacheIndex;
			}
			break;
		}
	}
}

static void CL_Cache_SetIndex_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Error( ERR_DROP, "setindex needs an index\n" );
		return;
	}

	cacheIndex = atoi( Cmd_Argv( 1 ) );
}

static void CL_Cache_MapChange_f( void ) {
	cacheIndex++;
}

static void CL_Cache_EndGather_f( void ) {
	// save the frequently used files to the cache list file
	int i, j, handle, cachePass;
	char filename[MAX_QPATH];

	cachePass = (int)floor( (float)cacheIndex * CACHE_HIT_RATIO );

	for ( i = 0; i < CACHE_NUMGROUPS; i++ ) {
		Q_strncpyz( filename, cacheGroups[i].name, MAX_QPATH );
		Q_strcat( filename, MAX_QPATH, ".cache" );

		handle = FS_FOpenFileWrite( filename );

		for ( j = 0; j < MAX_CACHE_ITEMS; j++ ) {
			// if it's a valid filename, and it's been hit enough times, cache it
			if ( cacheItems[i][j].hits >= cachePass && strstr( cacheItems[i][j].name, "/" ) ) {
				FS_Write( cacheItems[i][j].name, strlen( cacheItems[i][j].name ), handle );
				FS_Write( "\n", 1, handle );
			}
		}

		FS_FCloseFile( handle );
	}

	Cvar_Set( "cl_cacheGathering", "0" );
}

// done.
//============================================================================

/*
================
CL_SetRecommended_f
================
*/
void CL_SetRecommended_f( void ) {
	Com_SetRecommended();
}



/*
================
CL_RefPrintf

DLL glue
================
*/
static __attribute__ ((format (printf, 2, 3))) void QDECL CL_RefPrintf( int print_level, const char *fmt, ...) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	
	va_start (argptr,fmt);
	Q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	if ( print_level == PRINT_ALL ) {
		Com_Printf ("%s", msg);
	} else if ( print_level == PRINT_WARNING ) {
		Com_Printf (S_COLOR_YELLOW "%s", msg);		// yellow
	} else if ( print_level == PRINT_DEVELOPER ) {
		Com_DPrintf (S_COLOR_RED "%s", msg);		// red
	}
}



/*
============
CL_ShutdownRef
============
*/
void CL_ShutdownRef( void ) {
	if ( re.Shutdown ) {
		re.Shutdown( qtrue );
	}

	Com_Memset( &re, 0, sizeof( re ) );
}

/*
============
CL_InitRenderer
============
*/
void CL_InitRenderer( void ) {
	// this sets up the renderer and calls R_Init
	re.BeginRegistration( &cls.glconfig );

	// load character sets
	cls.charSetShader = re.RegisterShader( "gfx/2d/consolechars" );
	cls.whiteShader = re.RegisterShader( "white" );

// JPW NERVE

	cls.consoleShader = re.RegisterShader( "console-16bit" ); // JPW NERVE shader works with 16bit
	cls.consoleShader2 = re.RegisterShader( "console2-16bit" ); // JPW NERVE same

	g_console_field_width = cls.glconfig.vidWidth / SMALLCHAR_WIDTH - 2;
	g_consoleField.widthInChars = g_console_field_width;
}

/*
============================
CL_StartHunkUsers

After the server has cleared the hunk, these will need to be restarted
This is the only place that any of these functions are called from
============================
*/
void CL_StartHunkUsers( void ) {
	if ( !com_cl_running ) {
		return;
	}

	if ( !com_cl_running->integer ) {
		return;
	}

	// fixup renderer -EC-
	if ( !re.BeginRegistration ) {
		CL_InitRef();
	}

	if ( !cls.rendererStarted ) {
		cls.rendererStarted = qtrue;
		CL_InitRenderer();
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
		S_Init();
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration();
	}

	if ( !cls.uiStarted ) {
		cls.uiStarted = qtrue;
		CL_InitUI();
	}
}

// DHM - Nerve
void CL_CheckAutoUpdate( void ) {
#if 0
#ifndef PRE_RELEASE_DEMO

	if ( !cl_autoupdate->integer ) {
		return;
	}

	// Only check once per session
	if ( autoupdateChecked ) {
		return;
	}

	srand( Com_Milliseconds() );

	// Resolve update server
	if ( !NET_StringToAdr( cls.autoupdateServerNames[0], &cls.autoupdateServer  ) ) {
		Com_DPrintf( "Failed to resolve any Auto-update servers.\n" );

		cls.autoUpdateServerChecked[0] = qtrue;

		autoupdateChecked = qtrue;
		return;
	}

	cls.autoupdatServerIndex = 0;

	cls.autoupdatServerFirstIndex = cls.autoupdatServerIndex;

	cls.autoUpdateServerChecked[cls.autoupdatServerIndex] = qtrue;

	cls.autoupdateServer.port = BigShort( PORT_SERVER );
	Com_DPrintf( "autoupdate server at: %i.%i.%i.%i:%i\n", cls.autoupdateServer.ip[0], cls.autoupdateServer.ip[1],
				 cls.autoupdateServer.ip[2], cls.autoupdateServer.ip[3],
				 BigShort( cls.autoupdateServer.port ) );

	NET_OutOfBandPrint( NS_CLIENT, cls.autoupdateServer, "getUpdateInfo \"%s\" \"%s\"\n", Q3_VERSION, CPUSTRING );

#endif // !PRE_RELEASE_DEMO

	CL_RequestMotd();

	autoupdateChecked = qtrue;
#endif
}

qboolean CL_NextUpdateServer( void ) {
	return qfalse;
#if 0
	char        *servername;

#ifdef PRE_RELEASE_DEMO
	return qfalse;
#endif // PRE_RELEASE_DEMO

	if ( !cl_autoupdate->integer ) {
		return qfalse;
	}

#ifdef _DEBUG
	Com_Printf( S_COLOR_MAGENTA "Autoupdate hardcoded OFF in debug build\n" );
	return qfalse;
#endif

	while ( cls.autoUpdateServerChecked[cls.autoupdatServerFirstIndex] ) {
		cls.autoupdatServerIndex++;

		if ( cls.autoupdatServerIndex > MAX_AUTOUPDATE_SERVERS ) {
			cls.autoupdatServerIndex = 0;
		}

		if ( cls.autoupdatServerIndex == cls.autoupdatServerFirstIndex ) {
			// went through all of them already
			return qfalse;
		}
	}

	servername = cls.autoupdateServerNames[cls.autoupdatServerIndex];

	Com_DPrintf( "Resolving AutoUpdate Server... " );
	if ( !NET_StringToAdr( servername, &cls.autoupdateServer  ) ) {
		Com_DPrintf( "Couldn't resolve address, trying next one..." );

		cls.autoUpdateServerChecked[cls.autoupdatServerIndex] = qtrue;

		return CL_NextUpdateServer();
	}

	cls.autoUpdateServerChecked[cls.autoupdatServerIndex] = qtrue;

	cls.autoupdateServer.port = BigShort( PORT_SERVER );
	Com_DPrintf( "%i.%i.%i.%i:%i\n", cls.autoupdateServer.ip[0], cls.autoupdateServer.ip[1],
				 cls.autoupdateServer.ip[2], cls.autoupdateServer.ip[3],
				 BigShort( cls.autoupdateServer.port ) );

	return qtrue;
#endif
}

void CL_GetAutoUpdate( void ) {
#if 0
	// Don't try and get an update if we haven't checked for one
	if ( !autoupdateChecked ) {
		return;
	}

	// Make sure there's a valid update file to request
	if ( strlen( cl_updatefiles->string ) < 5 ) {
		return;
	}

	Com_DPrintf( "Connecting to auto-update server...\n" );

	S_StopAllSounds();      // NERVE - SMF

	// starting to load a map so we get out of full screen ui mode
	Cvar_Set( "r_uiFullScreen", "0" );

	// toggle on all the download related cvars
	Cvar_Set( "cl_allowDownload", "1" ); // general flag
	Cvar_Set( "cl_wwwDownload", "1" ); // ftp/http support

	// clear any previous "server full" type messages
	clc.serverMessage[0] = 0;

	if ( com_sv_running->integer ) {
		// if running a local server, kill it
		SV_Shutdown( "Server quit\n" );
	}

	// make sure a local server is killed
	Cvar_Set( "sv_killserver", "1" );
	SV_Frame( 0 );

	CL_Disconnect( qtrue );
	Con_Close();

	Q_strncpyz( cls.servername, "Auto-Updater", sizeof( cls.servername ) );

	if ( cls.autoupdateServer.type == NA_BAD ) {
		Com_Printf( "Bad server address\n" );
		cls.state = CA_DISCONNECTED;
		Cvar_Set( "ui_connecting", "0" );
		return;
	}

	// Copy auto-update server address to Server connect address
	memcpy( &clc.serverAddress, &cls.autoupdateServer, sizeof( netadr_t ) );

	Com_DPrintf( "%s resolved to %i.%i.%i.%i:%i\n", cls.servername,
				 clc.serverAddress.ip[0], clc.serverAddress.ip[1],
				 clc.serverAddress.ip[2], clc.serverAddress.ip[3],
				 BigShort( clc.serverAddress.port ) );

	cls.state = CA_CONNECTING;

	Key_SetCatcher( 0 );
	clc.connectTime = -99999;   // CL_CheckForResend() will fire immediately
	clc.connectPacketCount = 0;

	// server connection string
	Cvar_Set( "cl_currentServerAddress", "Auto-Updater" );
#endif
}
// DHM - Nerve

/*
============
CL_RefMalloc
============
*/
void *CL_RefMalloc( int size ) {
	return Z_TagMalloc( size, TAG_RENDERER );
}

/*
============
CL_RefTagFree
============
*/
void CL_RefTagFree( void ) {
	Z_FreeTags( TAG_RENDERER );
	return;
}

int CL_ScaledMilliseconds(void) {
	return Sys_Milliseconds()*com_timescale->value;
}

/*
============
CL_InitRef
============
*/
void CL_InitRef( void ) {
	refimport_t ri;
	refexport_t *ret;

	Com_Printf( "----- Initializing Renderer ----\n" );

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Printf = CL_RefPrintf;
	ri.Error = Com_Error;
	ri.Milliseconds = CL_ScaledMilliseconds;
	ri.Malloc = CL_RefMalloc;
	ri.Free = Z_Free;
	ri.Tag_Free = CL_RefTagFree;
	ri.Hunk_Clear = Hunk_ClearToMark;
#ifdef HUNK_DEBUG
	ri.Hunk_AllocDebug = Hunk_AllocDebug;
#else
	ri.Hunk_Alloc = Hunk_Alloc;
#endif
	ri.Hunk_AllocateTempMemory = Hunk_AllocateTempMemory;
	ri.Hunk_FreeTempMemory = Hunk_FreeTempMemory;
	
	ri.CM_ClusterPVS = CM_ClusterPVS;
	ri.CM_DrawDebugSurface = CM_DrawDebugSurface;

	ri.FS_ReadFile = FS_ReadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_WriteFile = FS_WriteFile;
	ri.FS_FreeFileList = FS_FreeFileList;
	ri.FS_ListFiles = FS_ListFiles;
	//ri.FS_FileIsInPAK = FS_FileIsInPAK;
	ri.FS_FileExists = FS_FileExists;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Cvar_CheckRange = Cvar_CheckRange;
	ri.Cvar_SetDescription = Cvar_SetDescription;
	ri.Cvar_VariableIntegerValue = Cvar_VariableIntegerValue;

	// cinematic stuff

	ri.CIN_UploadCinematic = CIN_UploadCinematic;
	ri.CIN_PlayCinematic = CIN_PlayCinematic;
	ri.CIN_RunCinematic = CIN_RunCinematic;
  
	ri.CL_WriteAVIVideoFrame = CL_WriteAVIVideoFrame;

	ret = GetRefAPI( REF_API_VERSION, &ri );

	Com_Printf( "-------------------------------\n");

	if ( !ret ) {
		Com_Error (ERR_FATAL, "Couldn't initialize refresh" );
	}

	re = *ret;

	// unpause so the cgame definately gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}

// RF, trap manual client damage commands so users can't issue them manually
void CL_ClientDamageCommand( void ) {
	// do nothing
}

// NERVE - SMF
/*void CL_startSingleplayer_f( void ) {
#if defined(__linux__)
	Sys_StartProcess( "./wolfsp.x86", qtrue );
#else
	Sys_StartProcess( "WolfSP.exe", qtrue );
#endif
}*/

// NERVE - SMF
// fretn unused
#if 0
void CL_buyNow_f( void ) {
	Sys_OpenURL( "http://www.activision.com/games/wolfenstein/purchase.html", qtrue );
}

// NERVE - SMF
void CL_singlePlayLink_f( void ) {
	Sys_OpenURL( "http://www.activision.com/games/wolfenstein/home.html", qtrue );
}
#endif

#if !defined( __MACOS__ )
void CL_SaveTranslations_f( void ) {
	CL_SaveTransTable( "scripts/translation.cfg", qfalse );
}

void CL_SaveNewTranslations_f( void ) {
	char fileName[512];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: SaveNewTranslations <filename>\n" );
		return;
	}

	strcpy( fileName, va( "translations/%s.cfg", Cmd_Argv( 1 ) ) );

	CL_SaveTransTable( fileName, qtrue );
}

void CL_LoadTranslations_f( void ) {
	CL_ReloadTranslation();
}
// -NERVE - SMF
#endif

//===========================================================================================

/*
===============
CL_Video_f

video
video [filename]
===============
*/
void CL_Video_f( void )
{
	char  filename[ MAX_QPATH ];
	int   i;

	if( !clc.demoplaying )
	{
		Com_Printf( "The video command can only be used when playing back demos\n" );
		return;
	}

	if( Cmd_Argc() == 2 )
	{
		// explicit filename
		Com_sprintf( filename, sizeof( filename ), "videos/%s", Cmd_Argv( 1 ) );
	}
	else
	{
		 // scan for a free filename
		for( i = 0; i <= 9999; i++ )
		{
			Com_sprintf( filename, sizeof( filename ), "videos/video%04d.avi", i );
			if ( !FS_FileExists( filename ) )
				break; // file doesn't exist
		}

		if( i > 9999 )
		{
			Com_Printf( S_COLOR_RED "ERROR: no free file names to create video\n" );
			return;
		}

		// without extension
		Com_sprintf( filename, sizeof( filename ), "videos/video%04d", i );
	}


	clc.aviSoundFrameRemainder = 0.0f;
	clc.aviVideoFrameRemainder = 0.0f;

	Q_strncpyz( clc.videoName, filename, sizeof( clc.videoName ) );
	clc.videoIndex = 0;

	CL_OpenAVIForWriting( va( "%s.avi", clc.videoName ) );
}


/*
===============
CL_StopVideo_f
===============
*/
void CL_StopVideo_f( void )
{
  CL_CloseAVI( );
}


/*
====================
CL_CompleteRecordName
====================
*/
static void CL_CompleteVideoName( char *args, int argNum )
{
	if( argNum == 2 )
	{
		Field_CompleteFilename( "videos", ".avi", qtrue, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}

static void CL_AddFavorite_f( void ) {
	const qboolean connected = (cls.state == CA_ACTIVE) && !clc.demoplaying;
	const int argc = Cmd_Argc();
	if ( !connected && argc != 2 ) {
		Com_Printf( "syntax: addFavorite <ip or hostname>\n" );
		return;
	}

	const char *server = (argc == 2) ? Cmd_Argv( 1 ) : NET_AdrToString( &clc.serverAddress );
	const int status = LAN_AddFavAddr( server );
	switch ( status ) {
	case -1:
		Com_Printf( "error adding favorite server: too many favorite servers\n" );
		break;
	case 0:
		Com_Printf( "error adding favorite server: server already exists\n" );
		break;
	case 1:
		Com_Printf( "successfully added favorite server \"%s\"\n", server );
		break;
	default:
		Com_Printf( "unknown error (%i) adding favorite server\n", status );
		break;
	}
}

/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
	Com_Printf( "----- Client Initialization -----\n" );

	Con_Init();

	CL_ClearState();
	cls.state = CA_DISCONNECTED;	// no longer CA_UNINITIALIZED

	CL_ResetOldGame();

	cls.realtime = 0;

	CL_InitInput();

	//
	// register our variables
	//
	cl_noprint = Cvar_Get( "cl_noprint", "0", 0 );
	cl_motd = Cvar_Get( "cl_motd", "1", 0 );
	cl_autoupdate = Cvar_Get( "cl_autoupdate", "1", CVAR_ARCHIVE );

	cl_timeout = Cvar_Get( "cl_timeout", "200", 0 );

	cl_wavefilerecord = Cvar_Get( "cl_wavefilerecord", "0", CVAR_TEMP );

	cl_timeNudge = Cvar_Get( "cl_timeNudge", "0", CVAR_TEMP );
	Cvar_CheckRange( cl_timeNudge, -30, 30, qtrue );
	cl_shownet = Cvar_Get( "cl_shownet", "0", CVAR_TEMP );
	cl_shownuments = Cvar_Get( "cl_shownuments", "0", CVAR_TEMP );
	cl_visibleClients = Cvar_Get( "cl_visibleClients", "0", CVAR_TEMP );
	cl_showServerCommands = Cvar_Get( "cl_showServerCommands", "0", 0 );
	cl_showSend = Cvar_Get( "cl_showSend", "0", CVAR_TEMP );
	cl_showTimeDelta = Cvar_Get( "cl_showTimeDelta", "0", CVAR_TEMP );
	cl_freezeDemo = Cvar_Get( "cl_freezeDemo", "0", CVAR_TEMP );
	rcon_client_password = Cvar_Get( "rconPassword", "", CVAR_TEMP );
	cl_activeAction = Cvar_Get( "activeAction", "", CVAR_TEMP );

	cl_timedemo = Cvar_Get ("timedemo", "0", 0);
	cl_autoRecordDemo = Cvar_Get ("cl_autoRecordDemo", "0", CVAR_ARCHIVE);

	cl_aviFrameRate = Cvar_Get ("cl_aviFrameRate", "25", CVAR_ARCHIVE);
	Cvar_CheckRange( cl_aviFrameRate, 1, 1000, qtrue );
	cl_aviMotionJpeg = Cvar_Get ("cl_aviMotionJpeg", "1", CVAR_ARCHIVE);
	cl_forceavidemo = Cvar_Get ("cl_forceavidemo", "0", 0);

	rconAddress = Cvar_Get( "rconAddress", "", 0 );

	cl_yawspeed = Cvar_Get ("cl_yawspeed", "140", CVAR_ARCHIVE_ND );
	cl_pitchspeed = Cvar_Get ("cl_pitchspeed", "140", CVAR_ARCHIVE_ND );
	cl_anglespeedkey = Cvar_Get ("cl_anglespeedkey", "1.5", 0);

	cl_maxpackets = Cvar_Get ("cl_maxpackets", "60", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_maxpackets, 15, 125, qtrue );
	cl_packetdup = Cvar_Get ("cl_packetdup", "1", CVAR_ARCHIVE_ND );

	cl_run = Cvar_Get( "cl_run", "1", CVAR_ARCHIVE_ND );
	cl_sensitivity = Cvar_Get ("sensitivity", "5", CVAR_ARCHIVE);
	cl_mouseAccel = Cvar_Get( "cl_mouseAccel", "0", CVAR_ARCHIVE_ND );
	cl_freelook = Cvar_Get( "cl_freelook", "1", CVAR_ARCHIVE_ND );

	// 0: legacy mouse acceleration
	// 1: new implementation
	cl_mouseAccelStyle = Cvar_Get( "cl_mouseAccelStyle", "0", CVAR_ARCHIVE_ND );
	// offset for the power function (for style 1, ignored otherwise)
	// this should be set to the max rate value
	cl_mouseAccelOffset = Cvar_Get( "cl_mouseAccelOffset", "5", CVAR_ARCHIVE_ND );
	Cvar_CheckRange(cl_mouseAccelOffset, 0.001f, 50000.0f, qfalse);

	cl_showMouseRate = Cvar_Get ("cl_showmouserate", "0", 0);

	cl_allowDownload = Cvar_Get( "cl_allowDownload", "1", CVAR_ARCHIVE_ND );
	cl_wwwDownload = Cvar_Get( "cl_wwwDownload", "1", CVAR_USERINFO | CVAR_ARCHIVE_ND );

	cl_profile = Cvar_Get( "cl_profile", "", CVAR_ROM );
	cl_defaultProfile = Cvar_Get( "cl_defaultProfile", "", CVAR_ROM );

	// init autoswitch so the ui will have it correctly even
	// if the cgame hasn't been started
	// -NERVE - SMF - disabled autoswitch by default
	Cvar_Get( "cg_autoswitch", "0", CVAR_ARCHIVE );

	// Rafael - particle switch
	Cvar_Get( "cg_wolfparticles", "1", CVAR_ARCHIVE );
	// done

	cl_conXOffset = Cvar_Get( "cl_conXOffset", "0", 0 );
	cl_conColor = Cvar_Get( "cl_conColor", "", 0 );
	cl_inGameVideo = Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE_ND );

	cl_serverStatusResendTime = Cvar_Get( "cl_serverStatusResendTime", "750", 0 );

	// RF
	cl_recoilPitch = Cvar_Get( "cg_recoilPitch", "0", CVAR_ROM );

	cl_bypassMouseInput = Cvar_Get( "cl_bypassMouseInput", "0", 0 ); //CVAR_ROM );			// NERVE - SMF

	cl_doubletapdelay = Cvar_Get( "cl_doubletapdelay", "350", CVAR_ARCHIVE ); // Arnout: double tap

	m_pitch = Cvar_Get( "m_pitch", "0.022", CVAR_ARCHIVE_ND );
	m_yaw = Cvar_Get( "m_yaw", "0.022", CVAR_ARCHIVE_ND );
	m_forward = Cvar_Get( "m_forward", "0.25", CVAR_ARCHIVE_ND );
	m_side = Cvar_Get( "m_side", "0.25", CVAR_ARCHIVE_ND );
	// ENSI TODO osx fix from q3e?
	m_filter = Cvar_Get( "m_filter", "0", CVAR_ARCHIVE_ND );

	cl_motdString = Cvar_Get( "cl_motdString", "", CVAR_ROM );

	//bani - make these cvars visible to cgame
	cl_demorecording = Cvar_Get( "cl_demorecording", "0", CVAR_ROM );
	cl_demofilename = Cvar_Get( "cl_demofilename", "", CVAR_ROM );
	cl_demooffset = Cvar_Get( "cl_demooffset", "0", CVAR_ROM );
	cl_waverecording = Cvar_Get( "cl_waverecording", "0", CVAR_ROM );
	cl_wavefilename = Cvar_Get( "cl_wavefilename", "", CVAR_ROM );
	cl_waveoffset = Cvar_Get( "cl_waveoffset", "0", CVAR_ROM );

	//bani
	cl_packetloss = Cvar_Get( "cl_packetloss", "0", CVAR_CHEAT );
	cl_packetdelay = Cvar_Get( "cl_packetdelay", "0", CVAR_CHEAT );

	Cvar_Get( "cl_maxPing", "800", CVAR_ARCHIVE_ND );

	cl_lanForcePackets = Cvar_Get( "cl_lanForcePackets", "1", CVAR_ARCHIVE_ND );

	// NERVE - SMF
	Cvar_Get( "cg_drawCompass", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_drawNotifyText", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_quickMessageAlt", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_popupLimboMenu", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_descriptiveText", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_drawTeamOverlay", "2", CVAR_ARCHIVE );
//	Cvar_Get( "cg_uselessNostalgia", "0", CVAR_ARCHIVE ); // JPW NERVE
	Cvar_Get( "cg_drawGun", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_cursorHints", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_voiceSpriteTime", "6000", CVAR_ARCHIVE );
//	Cvar_Get( "cg_teamChatsOnly", "0", CVAR_ARCHIVE );
//	Cvar_Get( "cg_noVoiceChats", "0", CVAR_ARCHIVE );
//	Cvar_Get( "cg_noVoiceText", "0", CVAR_ARCHIVE );
	Cvar_Get( "cg_crosshairSize", "48", CVAR_ARCHIVE );
	Cvar_Get( "cg_drawCrosshair", "1", CVAR_ARCHIVE );
	Cvar_Get( "cg_zoomDefaultSniper", "20", CVAR_ARCHIVE );
	Cvar_Get( "cg_zoomstepsniper", "2", CVAR_ARCHIVE );

//	Cvar_Get( "mp_playerType", "0", 0 );
//	Cvar_Get( "mp_currentPlayerType", "0", 0 );
//	Cvar_Get( "mp_weapon", "0", 0 );
//	Cvar_Get( "mp_team", "0", 0 );
//	Cvar_Get( "mp_currentTeam", "0", 0 );
	// -NERVE - SMF

	// userinfo
	Cvar_Get( "name", "ETPlayer", CVAR_USERINFO | CVAR_ARCHIVE_ND );
	Cvar_Get( "rate", "25000", CVAR_USERINFO | CVAR_ARCHIVE );     // NERVE - SMF - changed from 3000
	Cvar_Get( "snaps", "20", CVAR_USERINFO | CVAR_ARCHIVE );
//	Cvar_Get ("model", "american", CVAR_USERINFO | CVAR_ARCHIVE );	// temp until we have an skeletal american model
//	Arnout - no need // Cvar_Get ("model", "multi", CVAR_USERINFO | CVAR_ARCHIVE );
//	Arnout - no need // Cvar_Get ("head", "default", CVAR_USERINFO | CVAR_ARCHIVE );
//	Arnout - no need // Cvar_Get ("color", "4", CVAR_USERINFO | CVAR_ARCHIVE );
//	Arnout - no need // Cvar_Get ("handicap", "0", CVAR_USERINFO | CVAR_ARCHIVE );
//	Cvar_Get ("sex", "male", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "cl_anonymous", "0", CVAR_USERINFO | CVAR_ARCHIVE_ND );

	Cvar_Get( "password", "", CVAR_USERINFO );
	Cvar_Get( "cg_predictItems", "1", CVAR_ARCHIVE );

//----(SA) added
	Cvar_Get( "cg_autoactivate", "1", CVAR_ARCHIVE_ND );
//----(SA) end

	// cgame might not be initialized before menu is used
	Cvar_Get( "cg_viewsize", "100", CVAR_ARCHIVE_ND );
	// Make sure cg_stereoSeparation is zero as that variable is deprecated and should not be used anymore.
	Cvar_Get ("cg_stereoSeparation", "0", CVAR_ROM);


	Cvar_Get( "cg_autoReload", "1", CVAR_ARCHIVE_ND );

	// NERVE - SMF - localization
	cl_language = Cvar_Get( "cl_language", "0", CVAR_ARCHIVE_ND );
	cl_debugTranslation = Cvar_Get( "cl_debugTranslation", "0", 0 );
	// -NERVE - SMF

	// DHM - Nerve :: Auto-update
	cl_updateavailable = Cvar_Get( "cl_updateavailable", "0", CVAR_ROM );
	cl_updatefiles = Cvar_Get( "cl_updatefiles", "", CVAR_ROM );

	Q_strncpyz( cls.autoupdateServerNames[0], AUTOUPDATE_SERVER1_NAME, MAX_QPATH );
	Q_strncpyz( cls.autoupdateServerNames[1], AUTOUPDATE_SERVER2_NAME, MAX_QPATH );
	Q_strncpyz( cls.autoupdateServerNames[2], AUTOUPDATE_SERVER3_NAME, MAX_QPATH );
	Q_strncpyz( cls.autoupdateServerNames[3], AUTOUPDATE_SERVER4_NAME, MAX_QPATH );
	Q_strncpyz( cls.autoupdateServerNames[4], AUTOUPDATE_SERVER5_NAME, MAX_QPATH );
	// DHM - Nerve

	//
	// register our commands
	//
	Cmd_AddCommand( "cmd", CL_ForwardToServer_f );
	Cmd_AddCommand( "configstrings", CL_Configstrings_f );
	Cmd_AddCommand( "clientinfo", CL_Clientinfo_f );
	Cmd_AddCommand( "snd_reload", CL_Snd_Reload_f );
	Cmd_AddCommand( "snd_restart", CL_Snd_Restart_f );
	Cmd_AddCommand( "vid_restart", CL_Vid_Restart_f );
	Cmd_AddCommand( "ui_restart", CL_UI_Restart_f );          // NERVE - SMF
	Cmd_AddCommand( "disconnect", CL_Disconnect_f );
	Cmd_AddCommand( "record", CL_Record_f );
	Cmd_SetCommandCompletionFunc( "record", CL_CompleteRecordName );
	Cmd_AddCommand( "demo", CL_PlayDemo_f );
	Cmd_SetCommandCompletionFunc( "demo", CL_CompleteDemoName );
	Cmd_AddCommand( "cinematic", CL_PlayCinematic_f );
	Cmd_AddCommand( "stoprecord", CL_StopRecord_f );
	Cmd_AddCommand( "connect", CL_Connect_f );
	Cmd_AddCommand( "reconnect", CL_Reconnect_f );
	Cmd_AddCommand( "localservers", CL_LocalServers_f );
	Cmd_AddCommand( "globalservers", CL_GlobalServers_f );
	Cmd_AddCommand( "rcon", CL_Rcon_f );
	Cmd_SetCommandCompletionFunc( "rcon", CL_CompleteRcon );
	//Cmd_AddCommand( "setenv", CL_Setenv_f );
	Cmd_AddCommand( "ping", CL_Ping_f );
	Cmd_AddCommand( "serverstatus", CL_ServerStatus_f );
	Cmd_AddCommand( "showip", CL_ShowIP_f );
	Cmd_AddCommand( "fs_openedList", CL_OpenedPK3List_f );
	Cmd_AddCommand( "fs_referencedList", CL_ReferencedPK3List_f );

	Cmd_AddCommand ("video", CL_Video_f );
	Cmd_SetCommandCompletionFunc( "video", CL_CompleteVideoName );
	Cmd_AddCommand ("stopvideo", CL_StopVideo_f );

	// Ridah, startup-caching system
	Cmd_AddCommand( "cache_startgather", CL_Cache_StartGather_f );
	Cmd_AddCommand( "cache_usedfile", CL_Cache_UsedFile_f );
	Cmd_AddCommand( "cache_setindex", CL_Cache_SetIndex_f );
	Cmd_AddCommand( "cache_mapchange", CL_Cache_MapChange_f );
	Cmd_AddCommand( "cache_endgather", CL_Cache_EndGather_f );

	Cmd_AddCommand( "updatehunkusage", CL_UpdateLevelHunkUsage );
	Cmd_AddCommand( "updatescreen", SCR_UpdateScreen );
	// done.
#ifndef __MACOS__  //DAJ USA
	Cmd_AddCommand( "SaveTranslations", CL_SaveTranslations_f );     // NERVE - SMF - localization
	Cmd_AddCommand( "SaveNewTranslations", CL_SaveNewTranslations_f );   // NERVE - SMF - localization
	Cmd_AddCommand( "LoadTranslations", CL_LoadTranslations_f );     // NERVE - SMF - localization
#endif
	// NERVE - SMF - don't do this in multiplayer
	// RF, add this command so clients can't bind a key to send client damage commands to the server
//	Cmd_AddCommand ("cld", CL_ClientDamageCommand );

//	Cmd_AddCommand ( "startSingleplayer", CL_startSingleplayer_f );		// NERVE - SMF
//	fretn - unused
//	Cmd_AddCommand ( "buyNow", CL_buyNow_f );							// NERVE - SMF
//	Cmd_AddCommand ( "singlePlayLink", CL_singlePlayLink_f );			// NERVE - SMF

	Cmd_AddCommand( "setRecommended", CL_SetRecommended_f );

	//bani - we eat these commands to prevent exploits
	Cmd_AddCommand( "userinfo", CL_EatMe_f );

//	Cmd_AddCommand( "wav_record", CL_WavRecord_f );
//	Cmd_AddCommand( "wav_stoprecord", CL_WavStopRecord_f );

	Cmd_AddCommand( "addFavorite", CL_AddFavorite_f );

	CL_InitRef();

	SCR_Init ();

	//Cbuf_Execute ();

	Cvar_Set( "cl_running", "1" );

	// DHM - Nerve
	autoupdateChecked = qfalse;
	autoupdateStarted = qfalse;

#ifndef __MACOS__  //DAJ USA
	CL_InitTranslation();       // NERVE - SMF - localization
#endif

	Com_Printf( "----- Client Initialization Complete -----\n" );
}


/*
===============
CL_Shutdown

===============
*/
void CL_Shutdown( const char *finalmsg, qboolean quit ) {
	static qboolean recursive = qfalse;
	
	// check whether the client is running at all.
	if ( !( com_cl_running && com_cl_running->integer ) )
		return;
	
	Com_Printf( "----- Client Shutdown (%s) -----\n", finalmsg );

	if ( recursive ) {
		Com_Printf( "WARNING: Recursive shutdown\n" );
		return;
	}
	recursive = qtrue;

	//if ( clc.waverecording ) { // fretn - write wav header when we quit
	//	CL_WavStopRecord_f();
	//}

	noGameRestart = quit;
	CL_Disconnect( qfalse );

	CL_ShutdownVMs();

	S_Shutdown();
	DL_Shutdown();
	CL_ShutdownRef();

	Cmd_RemoveCommand ("cmd");
	Cmd_RemoveCommand ("configstrings");
	Cmd_RemoveCommand ("userinfo");
	Cmd_RemoveCommand ("clientinfo");
	Cmd_RemoveCommand( "snd_reload" );
	Cmd_RemoveCommand ("snd_restart");
	Cmd_RemoveCommand ("vid_restart");
	Cmd_RemoveCommand ("disconnect");
	Cmd_RemoveCommand ("record");
	Cmd_RemoveCommand ("demo");
	Cmd_RemoveCommand ("cinematic");
	Cmd_RemoveCommand ("stoprecord");
	Cmd_RemoveCommand ("connect");
	Cmd_RemoveCommand ("reconnect");
	Cmd_RemoveCommand ("localservers");
	Cmd_RemoveCommand ("globalservers");
	Cmd_RemoveCommand ("rcon");
	//Cmd_RemoveCommand( "setenv" );
	Cmd_RemoveCommand ("ping");
	Cmd_RemoveCommand ("serverstatus");
	Cmd_RemoveCommand ("showip");
	Cmd_RemoveCommand ("fs_openedList");
	Cmd_RemoveCommand ("fs_referencedList");
	//Cmd_RemoveCommand( "model" );
	Cmd_RemoveCommand ("video");
	Cmd_RemoveCommand ("stopvideo");

	// Ridah, startup-caching system
	Cmd_RemoveCommand( "cache_startgather" );
	Cmd_RemoveCommand( "cache_usedfile" );
	Cmd_RemoveCommand( "cache_setindex" );
	Cmd_RemoveCommand( "cache_mapchange" );
	Cmd_RemoveCommand( "cache_endgather" );

	Cmd_RemoveCommand( "updatehunkusage" );
	Cmd_RemoveCommand( "wav_record" );
	Cmd_RemoveCommand( "wav_stoprecord" );
	// done.

	Cmd_RemoveCommand( "addFavorite" );

	CL_ClearInput();
	Cvar_Set( "cl_running", "0" );

	recursive = qfalse;

	Com_Memset( &cls, 0, sizeof( cls ) );
	Key_SetCatcher( 0 );
	Com_Printf( "-----------------------\n" );
}


static void CL_SetServerInfo( serverInfo_t *server, const char *info, int ping ) {
	if ( server ) {
		if ( info ) {
			server->clients = atoi( Info_ValueForKey( info, "clients" ) );
			Q_strncpyz( server->hostName,Info_ValueForKey( info, "hostname" ), MAX_NAME_LENGTH );
			server->load = atoi( Info_ValueForKey( info, "serverload" ) );
			Q_strncpyz( server->mapName, Info_ValueForKey( info, "mapname" ), MAX_NAME_LENGTH );
			server->maxClients = atoi( Info_ValueForKey( info, "sv_maxclients" ) );
			Q_strncpyz( server->game,Info_ValueForKey( info, "game" ), MAX_NAME_LENGTH );
			server->gameType = atoi( Info_ValueForKey( info, "gametype" ) );
			server->netType = atoi( Info_ValueForKey( info, "nettype" ) );
			server->minPing = atoi( Info_ValueForKey( info, "minping" ) );
			server->maxPing = atoi( Info_ValueForKey( info, "maxping" ) );
			server->allowAnonymous = atoi( Info_ValueForKey( info, "sv_allowAnonymous" ) );
			server->friendlyFire = atoi( Info_ValueForKey( info, "friendlyFire" ) );         // NERVE - SMF
			server->maxlives = atoi( Info_ValueForKey( info, "maxlives" ) );                 // NERVE - SMF
			server->needpass = atoi( Info_ValueForKey( info, "needpass" ) );                 // NERVE - SMF
			server->punkbuster = atoi( Info_ValueForKey( info, "punkbuster" ) );             // DHM - Nerve
			Q_strncpyz( server->gameName, Info_ValueForKey( info, "gamename" ), MAX_NAME_LENGTH );   // Arnout
			server->antilag = atoi( Info_ValueForKey( info, "g_antilag" ) );
			server->weaprestrict = atoi( Info_ValueForKey( info, "weaprestrict" ) );
			server->balancedteams = atoi( Info_ValueForKey( info, "balancedteams" ) );
		}
		server->ping = ping;
	}
}


static void CL_SetServerInfoByAddress(const netadr_t *from, const char *info, int ping) {
	int i;

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		if (NET_CompareAdr(from, &cls.localServers[i].adr) ) {
			CL_SetServerInfo(&cls.localServers[i], info, ping);
		}
	}

	for (i = 0; i < MAX_GLOBAL_SERVERS; i++) {
		if (NET_CompareAdr(from, &cls.globalServers[i].adr)) {
			CL_SetServerInfo(&cls.globalServers[i], info, ping);
		}
	}

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		if (NET_CompareAdr(from, &cls.favoriteServers[i].adr)) {
			CL_SetServerInfo(&cls.favoriteServers[i], info, ping);
		}
	}

}


/*
===================
CL_ServerInfoPacket
===================
*/
static void CL_ServerInfoPacket( const netadr_t *from, msg_t *msg ) {
	int		i, type;
	char	info[MAX_INFO_STRING];
	const char *infoString;
	const char *gameName;
	int		prot;

	infoString = MSG_ReadString( msg );

	// if this isn't the correct protocol version, ignore it
	prot = atoi( Info_ValueForKey( infoString, "protocol" ) );
	if ( prot != PROTOCOL_VERSION ) {
		Com_DPrintf( "Different protocol info packet: %s\n", infoString );
		return;
	}

	// Arnout: if this isn't the correct game, ignore it
	gameName = Info_ValueForKey( infoString, "gamename" );
	if ( !gameName[0] || Q_stricmp( gameName, GAMENAME_STRING ) ) {
		Com_DPrintf( "Different game info packet: %s\n", infoString );
		return;
	}

	// iterate servers waiting for ping response
	for (i=0; i<MAX_PINGREQUESTS; i++)
	{
		if ( cl_pinglist[i].adr.port && !cl_pinglist[i].time && NET_CompareAdr( from, &cl_pinglist[i].adr ) )
		{
			// calc ping time
			cl_pinglist[i].time = Sys_Milliseconds() - cl_pinglist[i].start;
			if ( com_developer->integer ) 
			{
				Com_Printf( "ping time %dms from %s\n", cl_pinglist[i].time, NET_AdrToString( from ) );
			}

			// save of info
			Q_strncpyz( cl_pinglist[i].info, infoString, sizeof( cl_pinglist[i].info ) );

			// tack on the net type
			// NOTE: make sure these types are in sync with the netnames strings in the UI
			switch (from->type)
			{
				case NA_BROADCAST:
				case NA_IP:
					type = 1;
					break;
				case NA_IP6:
					type = 2;
					break;
				default:
					type = 0;
					break;
			}
			Info_SetValueForKey( cl_pinglist[i].info, "nettype", va("%d", type) );
			CL_SetServerInfoByAddress(from, infoString, cl_pinglist[i].time);

			return;
		}
	}

	// if not just sent a local broadcast or pinging local servers
	if (cls.pingUpdateSource != AS_LOCAL) {
		return;
	}

	for ( i = 0 ; i < MAX_OTHER_SERVERS ; i++ ) {
		// empty slot
		if ( cls.localServers[i].adr.port == 0 ) {
			break;
		}

		// avoid duplicate
		if ( NET_CompareAdr( from, &cls.localServers[i].adr ) ) {
			return;
		}
	}

	if ( i == MAX_OTHER_SERVERS ) {
		Com_DPrintf( "MAX_OTHER_SERVERS hit, dropping infoResponse\n" );
		return;
	}

	// add this to the list
	cls.numlocalservers = i+1;
	CL_InitServerInfo( &cls.localServers[i], from );
									 
	Q_strncpyz( info, MSG_ReadString( msg ), sizeof( info ) );
	if (strlen(info)) {
		if (info[strlen(info)-1] != '\n') {
			Q_strcat(info, sizeof(info), "\n");
		}
		Com_Printf( "%s: %s", NET_AdrToStringwPort( from ), info );
	}
}

/*
===================
CL_UpdateInfoPacket
===================
*/
void CL_UpdateInfoPacket( const netadr_t *from ) {

	if ( cls.autoupdateServer.type == NA_BAD ) {
		Com_DPrintf( "CL_UpdateInfoPacket:  Auto-Updater has bad address\n" );
		return;
	}

	Com_DPrintf( "Auto-Updater resolved to %i.%i.%i.%i:%i\n",
				 cls.autoupdateServer.ip[0], cls.autoupdateServer.ip[1],
				 cls.autoupdateServer.ip[2], cls.autoupdateServer.ip[3],
				 BigShort( cls.autoupdateServer.port ) );

	if ( !NET_CompareAdr( from, &cls.autoupdateServer ) ) {
		Com_DPrintf( "CL_UpdateInfoPacket:  Received packet from %s\n",
					 NET_AdrToStringwPort( from ) );
		return;
	}

	Cvar_Set( "cl_updateavailable", Cmd_Argv( 1 ) );

	if ( !Q_stricmp( cl_updateavailable->string, "1" ) ) {
		Cvar_Set( "cl_updatefiles", Cmd_Argv( 2 ) );
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_WM_AUTOUPDATE );
	}
}
// DHM - Nerve

/*
===================
CL_GetServerStatus
===================
*/
static serverStatus_t *CL_GetServerStatus( const netadr_t *from ) {
	int i, oldest, oldestTime;

	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if ( NET_CompareAdr( from, &cl_serverStatusList[i].address ) ) {
			return &cl_serverStatusList[i];
		}
	}
	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if ( cl_serverStatusList[i].retrieved ) {
			return &cl_serverStatusList[i];
		}
	}
	oldest = -1;
	oldestTime = 0;
	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if (oldest == -1 || cl_serverStatusList[i].startTime < oldestTime) {
			oldest = i;
			oldestTime = cl_serverStatusList[i].startTime;
		}
	}
	return &cl_serverStatusList[oldest];
}


/*
===================
CL_ServerStatus
===================
*/
int CL_ServerStatus( char *serverAddress, char *serverStatusString, int maxLen ) {
	int i;
	netadr_t	to;
	serverStatus_t *serverStatus;

	// if no server address then reset all server status requests
	if ( !serverAddress ) {
		for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
			cl_serverStatusList[i].address.port = 0;
			cl_serverStatusList[i].retrieved = qtrue;
		}
		return qfalse;
	}
	// get the address
	if ( !NET_StringToAdr( serverAddress, &to, NA_UNSPEC ) ) {
		return qfalse;
	}
	serverStatus = CL_GetServerStatus( &to );
	// if no server status string then reset the server status request for this address
	if ( !serverStatusString ) {
		serverStatus->retrieved = qtrue;
		return qfalse;
	}

	// if this server status request has the same address
	if ( NET_CompareAdr( &to, &serverStatus->address) ) {
		// if we received a response for this server status request
		if (!serverStatus->pending) {
			Q_strncpyz(serverStatusString, serverStatus->string, maxLen);
			serverStatus->retrieved = qtrue;
			serverStatus->startTime = 0;
			return qtrue;
		}
		// resend the request regularly
		else if ( serverStatus->startTime < Com_Milliseconds() - cl_serverStatusResendTime->integer ) {
			serverStatus->print = qfalse;
			serverStatus->pending = qtrue;
			serverStatus->retrieved = qfalse;
			serverStatus->time = 0;
			serverStatus->startTime = Com_Milliseconds();
			NET_OutOfBandPrint( NS_CLIENT, &to, "getstatus" );
			return qfalse;
		}
	}
	// if retrieved
	else if ( serverStatus->retrieved ) {
		serverStatus->address = to;
		serverStatus->print = qfalse;
		serverStatus->pending = qtrue;
		serverStatus->retrieved = qfalse;
		serverStatus->startTime = Com_Milliseconds();
		serverStatus->time = 0;
		NET_OutOfBandPrint( NS_CLIENT, &to, "getstatus" );
		return qfalse;
	}
	return qfalse;
}


/*
===================
CL_ServerStatusResponse
===================
*/
static void CL_ServerStatusResponse( const netadr_t *from, msg_t *msg ) {
	const char	*s;
	char	info[MAX_INFO_STRING];
	char	buf[64], *v[2];
	int		i, l, score, ping;
	int		len;
	serverStatus_t *serverStatus;

	serverStatus = NULL;
	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if ( NET_CompareAdr( from, &cl_serverStatusList[i].address ) ) {
			serverStatus = &cl_serverStatusList[i];
			break;
		}
	}
	// if we didn't request this server status
	if (!serverStatus) {
		return;
	}

	s = MSG_ReadStringLine( msg );

	len = 0;
	Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "%s", s);

	if (serverStatus->print) {
		Com_Printf("Server settings:\n");
		// print cvars
		while (*s) {
			for (i = 0; i < 2 && *s; i++) {
				if (*s == '\\')
					s++;
				l = 0;
				while (*s) {
					info[l++] = *s;
					if (l >= MAX_INFO_STRING-1)
						break;
					s++;
					if (*s == '\\') {
						break;
					}
				}
				info[l] = '\0';
				if (i) {
					Com_Printf("%s\n", info);
				}
				else {
					Com_Printf("%-24s", info);
				}
			}
		}
	}

	len = strlen(serverStatus->string);
	Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "\\");

	if (serverStatus->print) {
		Com_Printf("\nPlayers:\n");
		Com_Printf("num: score: ping: name:\n");
	}
	for (i = 0, s = MSG_ReadStringLine( msg ); *s; s = MSG_ReadStringLine( msg ), i++) {

		len = strlen(serverStatus->string);
		Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "\\%s", s);

		if (serverStatus->print) {
			//score = ping = 0;
			//sscanf(s, "%d %d", &score, &ping);
			Q_strncpyz( buf, s, sizeof (buf) );
			Com_Split( buf, v, 2, ' ' );
			score = atoi( v[0] );
			ping = atoi( v[1] );
			s = strchr(s, ' ');
			if (s)
				s = strchr(s+1, ' ');
			if (s)
				s++;
			else
				s = "unknown";
			Com_Printf("%-2d   %-3d    %-3d   %s\n", i, score, ping, s );
		}
	}
	len = strlen(serverStatus->string);
	Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "\\");

	serverStatus->time = Com_Milliseconds();
	serverStatus->address = *from;
	serverStatus->pending = qfalse;
	if (serverStatus->print) {
		serverStatus->retrieved = qtrue;
	}
}


/*
==================
CL_LocalServers_f
==================
*/
void CL_LocalServers_f( void ) {
	char		*message;
	int			i, j, n;
	netadr_t	to;

	Com_Printf( "Scanning for servers on the local network...\n");

	// reset the list, waiting for response
	cls.numlocalservers = 0;
	cls.pingUpdateSource = AS_LOCAL;

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		qboolean b = cls.localServers[i].visible;
		Com_Memset(&cls.localServers[i], 0, sizeof(cls.localServers[i]));
		cls.localServers[i].visible = b;
	}
	Com_Memset( &to, 0, sizeof( to ) );

	// The 'xxx' in the message is a challenge that will be echoed back
	// by the server.  We don't care about that here, but master servers
	// can use that to prevent spoofed server responses from invalid ip
	message = "\377\377\377\377getinfo xxx";
	n = (int)strlen( message );

	// send each message twice in case one is dropped
	for ( i = 0 ; i < 2 ; i++ ) {
		// send a broadcast packet on each server port
		// we support multiple server ports so a single machine
		// can nicely run multiple servers
		for ( j = 0 ; j < NUM_SERVER_PORTS ; j++ ) {
			to.port = BigShort( (short)(PORT_SERVER + j) );

			to.type = NA_BROADCAST;
			NET_SendPacket( NS_CLIENT, n, message, &to );
			to.type = NA_MULTICAST6;
			NET_SendPacket( NS_CLIENT, n, message, &to );
		}
	}
}

/*
==================
CL_GlobalServers_f
==================
*/
void CL_GlobalServers_f( void ) {
	netadr_t	to;
	int			count, i, masterNum;
	char		command[1024];
	const char	*masteraddress;
	char		*cmdname;
	
	if ( (count = Cmd_Argc()) < 3 || (masterNum = atoi(Cmd_Argv(1))) < 0 || masterNum > MAX_MASTER_SERVERS - 1 )
	{
		Com_Printf( "usage: globalservers <master# 0-%d> <protocol> [keywords]\n", MAX_MASTER_SERVERS - 1 );
		return;	
	}

	sprintf(command, "sv_master%d", masterNum + 1);
	masteraddress = Cvar_VariableString( command );
	
	if ( !*masteraddress )
	{
		Com_Printf( "CL_GlobalServers_f: Error: No master server address given.\n");
		return;	
	}

	// reset the list, waiting for response
	// -1 is used to distinguish a "no response"

	i = NET_StringToAdr( masteraddress, &to, NA_UNSPEC );
	
	if ( !i )
	{
		Com_Printf( "CL_GlobalServers_f: Error: could not resolve address of master %s\n", masteraddress);
		return;	
	}
	else if ( i == 2 )
		to.port = BigShort( PORT_MASTER );

	Com_Printf( "Requesting servers from master %s...\n", masteraddress );

	cls.numglobalservers = -1;
	cls.pingUpdateSource = AS_GLOBAL;

	// Use the extended query for IPv6 masters
	if (to.type == NA_IP6 || to.type == NA_MULTICAST6)
	{
		cmdname = "getserversExt " "ET"/*GAMENAME_FOR_MASTER*/;

		// TODO: test if we only have an IPv6 connection. If it's the case,
		//       request IPv6 servers only by appending " ipv6" to the command
	}
	else
		cmdname = "getservers";
	Com_sprintf( command, sizeof(command), "%s %s", cmdname, Cmd_Argv(2) );

	for (i=3; i < count; i++)
	{
		Q_strcat(command, sizeof(command), " ");
		Q_strcat(command, sizeof(command), Cmd_Argv(i));
	}
	
	// if we are a demo, automatically add a "demo" keyword
//	if ( Cvar_VariableValue( "fs_restrict" ) ) {
//		buffptr += sprintf( buffptr, " demo" );
//	}

	NET_OutOfBandPrint( NS_SERVER, &to, "%s", command );
}


/*
==================
CL_GetPing
==================
*/
void CL_GetPing( int n, char *buf, int buflen, int *pingtime )
{
	const char	*str;
	int		time;
	int		maxPing;

	if (n < 0 || n >= MAX_PINGREQUESTS || !cl_pinglist[n].adr.port)
	{
		// empty or invalid slot
		buf[0]    = '\0';
		*pingtime = 0;
		return;
	}

	str = NET_AdrToStringwPort( &cl_pinglist[n].adr );
	Q_strncpyz( buf, str, buflen );

	time = cl_pinglist[n].time;
	if (!time)
	{
		// check for timeout
		time = Sys_Milliseconds() - cl_pinglist[n].start;
		maxPing = Cvar_VariableIntegerValue( "cl_maxPing" );
		if( maxPing < 100 ) {
			maxPing = 100;
		}
		if (time < maxPing)
		{
			// not timed out yet
			time = 0;
		}
	}

	CL_SetServerInfoByAddress(&cl_pinglist[n].adr, cl_pinglist[n].info, cl_pinglist[n].time);

	*pingtime = time;
}

/*
==================
CL_GetPingInfo
==================
*/
void CL_GetPingInfo( int n, char *buf, int buflen )
{
	if (n < 0 || n >= MAX_PINGREQUESTS || !cl_pinglist[n].adr.port)
	{
		// empty or invalid slot
		if (buflen)
			buf[0] = '\0';
		return;
	}

	Q_strncpyz( buf, cl_pinglist[n].info, buflen );
}

/*
==================
CL_ClearPing
==================
*/
void CL_ClearPing( int n )
{
	if (n < 0 || n >= MAX_PINGREQUESTS)
		return;

	cl_pinglist[n].adr.port = 0;
}


/*
==================
CL_GetPingQueueCount
==================
*/
int CL_GetPingQueueCount( void )
{
	int		i;
	int		count;
	ping_t*	pingptr;

	count   = 0;
	pingptr = cl_pinglist;

	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ ) {
		if (pingptr->adr.port) {
			count++;
		}
	}

	return (count);
}

/*
==================
CL_GetFreePing
==================
*/
ping_t* CL_GetFreePing( void )
{
	ping_t*	pingptr;
	ping_t*	best;	
	int		oldest;
	int		i;
	int		time;

	pingptr = cl_pinglist;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// find free ping slot
		if (pingptr->adr.port)
		{
			if (!pingptr->time)
			{
				if (Sys_Milliseconds() - pingptr->start < 500)
				{
					// still waiting for response
					continue;
				}
			}
			else if (pingptr->time < 500)
			{
				// results have not been queried
				continue;
			}
		}

		// clear it
		pingptr->adr.port = 0;
		return (pingptr);
	}

	// use oldest entry
	pingptr = cl_pinglist;
	best    = cl_pinglist;
	oldest  = INT_MIN;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// scan for oldest
		time = Sys_Milliseconds() - pingptr->start;
		if (time > oldest)
		{
			oldest = time;
			best   = pingptr;
		}
	}

	return (best);
}


/*
==================
CL_Ping_f
==================
*/
void CL_Ping_f( void ) {
	netadr_t	to;
	ping_t*		pingptr;
	char*		server;
	int			argc;
	netadrtype_t	family = NA_UNSPEC;

	argc = Cmd_Argc();

	if ( argc != 2 && argc != 3 ) {
		Com_Printf( "usage: ping [-4|-6] server\n");
		return;	
	}
	
	if(argc == 2)
		server = Cmd_Argv(1);
	else
	{
		if(!strcmp(Cmd_Argv(1), "-4"))
			family = NA_IP;
		else if(!strcmp(Cmd_Argv(1), "-6"))
			family = NA_IP6;
		else
			Com_Printf( "warning: only -4 or -6 as address type understood.\n");
		
		server = Cmd_Argv(2);
	}

	Com_Memset( &to, 0, sizeof( to ) );

	if ( !NET_StringToAdr( server, &to, family ) ) {
		return;
	}

	pingptr = CL_GetFreePing();

	memcpy( &pingptr->adr, &to, sizeof (netadr_t) );
	pingptr->start = Sys_Milliseconds();
	pingptr->time  = 0;

	CL_SetServerInfoByAddress( &pingptr->adr, NULL, 0 );
		
	NET_OutOfBandPrint( NS_CLIENT, &to, "getinfo xxx" );
}


/*
==================
CL_UpdateVisiblePings_f
==================
*/
qboolean CL_UpdateVisiblePings_f(int source) {
	int			slots, i;
	char		buff[MAX_STRING_CHARS];
	int			pingTime;
	int			max;
	qboolean status = qfalse;

	if (source < 0 || source > AS_FAVORITES) {
		return qfalse;
	}

	cls.pingUpdateSource = source;

	slots = CL_GetPingQueueCount();
	if (slots < MAX_PINGREQUESTS) {
		serverInfo_t *server = NULL;

		switch (source) {
			case AS_LOCAL :
				server = &cls.localServers[0];
				max = cls.numlocalservers;
			break;
			case AS_GLOBAL :
				server = &cls.globalServers[0];
				max = cls.numglobalservers;
			break;
			case AS_FAVORITES :
				server = &cls.favoriteServers[0];
				max = cls.numfavoriteservers;
			break;
			default:
				return qfalse;
		}
		for (i = 0; i < max; i++) {
			if (server[i].visible) {
				if (server[i].ping == -1) {
					int j;

					if (slots >= MAX_PINGREQUESTS) {
						break;
					}
					for (j = 0; j < MAX_PINGREQUESTS; j++) {
						if (!cl_pinglist[j].adr.port) {
							continue;
						}
						if (NET_CompareAdr( &cl_pinglist[j].adr, &server[i].adr)) {
							// already on the list
							break;
						}
					}
					if (j >= MAX_PINGREQUESTS) {
						status = qtrue;
						for (j = 0; j < MAX_PINGREQUESTS; j++) {
							if (!cl_pinglist[j].adr.port) {
								break;
							}
						}
						memcpy(&cl_pinglist[j].adr, &server[i].adr, sizeof(netadr_t));
						cl_pinglist[j].start = Sys_Milliseconds();
						cl_pinglist[j].time = 0;
						NET_OutOfBandPrint( NS_CLIENT, &cl_pinglist[j].adr, "getinfo xxx" );
						slots++;
					}
				}
				// if the server has a ping higher than cl_maxPing or
				// the ping packet got lost
				else if (server[i].ping == 0) {
					// if we are updating global servers
					if (source == AS_GLOBAL) {
						//
						if ( cls.numGlobalServerAddresses > 0 ) {
							// overwrite this server with one from the additional global servers
							cls.numGlobalServerAddresses--;
							CL_InitServerInfo(&server[i], &cls.globalServerAddresses[cls.numGlobalServerAddresses]);
							// NOTE: the server[i].visible flag stays untouched
						}
					}
				}
			}
		}
	} 

	if (slots) {
		status = qtrue;
	}
	for (i = 0; i < MAX_PINGREQUESTS; i++) {
		if (!cl_pinglist[i].adr.port) {
			continue;
		}
		CL_GetPing( i, buff, MAX_STRING_CHARS, &pingTime );
		if (pingTime != 0) {
			CL_ClearPing(i);
			status = qtrue;
		}
	}

	return status;
}


/*
==================
CL_ServerStatus_f
==================
*/
static void CL_ServerStatus_f( void ) {
	netadr_t	to, *toptr = NULL;
	char		*server;
	serverStatus_t *serverStatus;
	int			argc;
	netadrtype_t	family = NA_UNSPEC;

	argc = Cmd_Argc();

	if ( argc != 2 && argc != 3 )
	{
		if (cls.state != CA_ACTIVE || clc.demoplaying)
		{
			Com_Printf( "Not connected to a server.\n" );
			Com_Printf( "usage: serverstatus [-4|-6] server\n" );
			return;
		}

		toptr = &clc.serverAddress;
	}
	
	if(!toptr)
	{
		Com_Memset( &to, 0, sizeof( to ) );
	
		if(argc == 2)
			server = Cmd_Argv(1);
		else
		{
			if(!strcmp(Cmd_Argv(1), "-4"))
				family = NA_IP;
			else if(!strcmp(Cmd_Argv(1), "-6"))
				family = NA_IP6;
			else
				Com_Printf( "warning: only -4 or -6 as address type understood.\n");
		
			server = Cmd_Argv(2);
		}

		toptr = &to;
		if ( !NET_StringToAdr( server, toptr, family ) )
			return;
	}

	NET_OutOfBandPrint( NS_CLIENT, toptr, "getstatus" );

	serverStatus = CL_GetServerStatus( toptr );
	serverStatus->address = *toptr;
	serverStatus->print = qtrue;
	serverStatus->pending = qtrue;
}


/*
==================
CL_ShowIP_f
==================
*/
static void CL_ShowIP_f(void) {
	Sys_ShowIP();
}

/*
=================
bool CL_CDKeyValidate
=================
*/
qboolean CL_CDKeyValidate( const char *key, const char *checksum ) {
	char ch;
	byte sum;
	char chs[3];
	int i, len;

	len = strlen( key );
	if ( len != CDKEY_LEN ) {
		return qfalse;
	}

	if ( checksum && strlen( checksum ) != CDCHKSUM_LEN ) {
		return qfalse;
	}

	sum = 0;
	// for loop gets rid of conditional assignment warning
	for ( i = 0; i < len; i++ ) {
		ch = *key++;
		if ( ch >= 'a' && ch <= 'z' ) {
			ch -= 32;
		}
		switch ( ch ) {
		case '2':
		case '3':
		case '7':
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'G':
		case 'H':
		case 'J':
		case 'L':
		case 'P':
		case 'R':
		case 'S':
		case 'T':
		case 'W':
			sum = ( sum << 1 ) ^ ch;
			continue;
		default:
			return qfalse;
		}
	}


	sprintf( chs, "%02x", sum );

	if ( checksum && !Q_stricmp( chs, checksum ) ) {
		return qtrue;
	}

	if ( !checksum ) {
		return qtrue;
	}

	return qfalse;
}

// NERVE - SMF
/*
=======================
CL_AddToLimboChat

=======================
*/
void CL_AddToLimboChat( const char *str ) {
	int len;
	char *p, *ls;
	int lastcolor;
	int chatHeight;
	int i;

	chatHeight = LIMBOCHAT_HEIGHT;
	cl.limboChatPos = LIMBOCHAT_HEIGHT - 1;
	len = 0;

	// copy old strings
	for ( i = cl.limboChatPos; i > 0; i-- ) {
		strcpy( cl.limboChatMsgs[i], cl.limboChatMsgs[i - 1] );
	}

	// copy new string
	p = cl.limboChatMsgs[0];
	*p = 0;

	lastcolor = '7';

	ls = NULL;
	while ( *str ) {
		if ( len > LIMBOCHAT_WIDTH - 1 ) {
			break;
		}

		if ( Q_IsColorString( str ) ) {
			*p++ = *str++;
			lastcolor = *str;
			*p++ = *str++;
			continue;
		}
		if ( *str == ' ' ) {
			ls = p;
		}
		*p++ = *str++;
		len++;
	}
	*p = 0;
}

/*
=======================
CL_GetLimboString

=======================
*/
qboolean CL_GetLimboString( int index, char *buf ) {
	if ( index >= LIMBOCHAT_HEIGHT ) {
		return qfalse;
	}

	strncpy( buf, cl.limboChatMsgs[index], 140 );
	return qtrue;
}
// -NERVE - SMF



// NERVE - SMF - Localization code
#define FILE_HASH_SIZE      1024
#define MAX_VA_STRING       32000
#define MAX_TRANS_STRING    4096

#ifndef __MACOS__   //DAJ USA
typedef struct trans_s {
	char original[MAX_TRANS_STRING];
	char translated[MAX_LANGUAGES][MAX_TRANS_STRING];
	struct  trans_s *next;
	float x_offset;
	float y_offset;
	qboolean fromFile;
} trans_t;

static trans_t* transTable[FILE_HASH_SIZE];

/*
=======================
AllocTrans
=======================
*/
static trans_t* AllocTrans( char *original, char *translated[MAX_LANGUAGES] ) {
	trans_t *t;
	int i;

	t = malloc( sizeof( trans_t ) );
	memset( t, 0, sizeof( trans_t ) );

	if ( original ) {
		strncpy( t->original, original, MAX_TRANS_STRING );
	}

	if ( translated ) {
		for ( i = 0; i < MAX_LANGUAGES; i++ )
			strncpy( t->translated[i], translated[i], MAX_TRANS_STRING );
	}

	return t;
}

/*
=======================
generateHashValue
=======================
*/
static long generateHashValue( const char *fname ) {
	int i;
	long hash;
	char letter;

	hash = 0;
	i = 0;
	while ( fname[i] != '\0' ) {
		letter = tolower( fname[i] );
		hash += (long)( letter ) * ( i + 119 );
		i++;
	}
	hash &= ( FILE_HASH_SIZE - 1 );
	return hash;
}

/*
=======================
LookupTrans
=======================
*/
static trans_t* LookupTrans( char *original, char *translated[MAX_LANGUAGES], qboolean isLoading ) {
	trans_t *t, *newt, *prev = NULL;
	long hash;

	hash = generateHashValue( original );

	for ( t = transTable[hash]; t; prev = t, t = t->next ) {
		if ( !Q_stricmp( original, t->original ) ) {
			if ( isLoading ) {
				Com_DPrintf( S_COLOR_YELLOW "WARNING: Duplicate string found: \"%s\"\n", original );
			}
			return t;
		}
	}

	newt = AllocTrans( original, translated );

	if ( prev ) {
		prev->next = newt;
	} else {
		transTable[hash] = newt;
	}

	if ( cl_debugTranslation->integer >= 1 && !isLoading ) {
		Com_Printf( "Missing translation: \'%s\'\n", original );
	}

	// see if we want to save out the translation table everytime a string is added
	//if ( cl_debugTranslation->integer == 2 && !isLoading ) {
	//	CL_SaveTransTable();
	//}

	return newt;
}

/*
=======================
CL_SaveTransTable
=======================
*/
void CL_SaveTransTable( const char *fileName, qboolean newOnly ) {
	int bucketlen, bucketnum, maxbucketlen, avebucketlen;
	int untransnum, transnum;
	const char *buf;
	fileHandle_t f;
	trans_t *t;
	int i, j, len;

	if ( cl.corruptedTranslationFile ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Cannot save corrupted translation file. Please reload first." );
		return;
	}

	FS_FOpenFileByMode( fileName, &f, FS_WRITE );

	bucketnum = 0;
	maxbucketlen = 0;
	avebucketlen = 0;
	transnum = 0;
	untransnum = 0;

	// write out version, if one
	if ( strlen( cl.translationVersion ) ) {
		buf = va( "#version\t\t\"%s\"\n", cl.translationVersion );
	} else {
		buf = va( "#version\t\t\"1.0 01/01/01\"\n" );
	}

	len = strlen( buf );
	FS_Write( buf, len, f );

	// write out translated strings
	for ( j = 0; j < 2; j++ ) {

		for ( i = 0; i < FILE_HASH_SIZE; i++ ) {
			t = transTable[i];

			if ( !t || ( newOnly && t->fromFile ) ) {
				continue;
			}

			bucketlen = 0;

			for ( ; t; t = t->next ) {
				bucketlen++;

				if ( strlen( t->translated[0] ) ) {
					if ( j ) {
						continue;
					}
					transnum++;
				} else {
					if ( !j ) {
						continue;
					}
					untransnum++;
				}

				buf = va( "{\n\tenglish\t\t\"%s\"\n", t->original );
				len = strlen( buf );
				FS_Write( buf, len, f );

				buf = va( "\tfrench\t\t\"%s\"\n", t->translated[LANGUAGE_FRENCH] );
				len = strlen( buf );
				FS_Write( buf, len, f );

				buf = va( "\tgerman\t\t\"%s\"\n", t->translated[LANGUAGE_GERMAN] );
				len = strlen( buf );
				FS_Write( buf, len, f );

				buf = va( "\titalian\t\t\"%s\"\n", t->translated[LANGUAGE_ITALIAN] );
				len = strlen( buf );
				FS_Write( buf, len, f );

				buf = va( "\tspanish\t\t\"%s\"\n", t->translated[LANGUAGE_SPANISH] );
				len = strlen( buf );
				FS_Write( buf, len, f );

				buf = "}\n";
				len = strlen( buf );
				FS_Write( buf, len, f );
			}

			if ( bucketlen > maxbucketlen ) {
				maxbucketlen = bucketlen;
			}

			if ( bucketlen ) {
				bucketnum++;
				avebucketlen += bucketlen;
			}
		}
	}

	Com_Printf( "Saved translation table.\nTotal = %i, Translated = %i, Untranslated = %i, aveblen = %2.2f, maxblen = %i\n",
				transnum + untransnum, transnum, untransnum, (float)avebucketlen / bucketnum, maxbucketlen );

	FS_FCloseFile( f );
}

/*
=======================
CL_CheckTranslationString

NERVE - SMF - compare formatting characters
=======================
*/
qboolean CL_CheckTranslationString( char *original, char *translated ) {
	char format_org[128], format_trans[128];
	int len, i;

	memset( format_org, 0, 128 );
	memset( format_trans, 0, 128 );

	// generate formatting string for original
	len = strlen( original );

	for ( i = 0; i < len; i++ ) {
		if ( original[i] != '%' ) {
			continue;
		}

		strcat( format_org, va( "%c%c ", '%', original[i + 1] ) );
	}

	// generate formatting string for translated
	len = strlen( translated );
	if ( !len ) {
		return qtrue;
	}

	for ( i = 0; i < len; i++ ) {
		if ( translated[i] != '%' ) {
			continue;
		}

		strcat( format_trans, va( "%c%c ", '%', translated[i + 1] ) );
	}

	// compare
	len = strlen( format_org );

	if ( len != strlen( format_trans ) ) {
		return qfalse;
	}

	for ( i = 0; i < len; i++ ) {
		if ( format_org[i] != format_trans[i] ) {
			return qfalse;
		}
	}

	return qtrue;
}

/*
=======================
CL_LoadTransTable
=======================
*/
void CL_LoadTransTable( const char *fileName ) {
	char translated[MAX_LANGUAGES][MAX_VA_STRING];
	char original[MAX_VA_STRING];
	qboolean aborted;
	char *text;
	fileHandle_t f;
	char *text_p;
	char *token;
	int len, i;
	trans_t *t;
	int count;

	count = 0;
	aborted = qfalse;
	cl.corruptedTranslationFile = qfalse;

	len = FS_FOpenFileByMode( fileName, &f, FS_READ );
	if ( len <= 0 ) {
		return;
	}

	// Gordon: shouldn't this be a z_malloc or something?
	text = malloc( len + 1 );
	if ( !text ) {
		return;
	}

	FS_Read( text, len, f );
	text[len] = 0;
	FS_FCloseFile( f );

	// parse the text
	text_p = text;

	do {
		token = COM_Parse( &text_p );
		if ( Q_stricmp( "{", token ) ) {
			// parse version number
			if ( !Q_stricmp( "#version", token ) ) {
				token = COM_Parse( &text_p );
				strcpy( cl.translationVersion, token );
				continue;
			}

			break;
		}

		// english
		token = COM_Parse( &text_p );
		if ( Q_stricmp( "english", token ) ) {
			aborted = qtrue;
			break;
		}

		token = COM_Parse( &text_p );
		strcpy( original, token );

		if ( cl_debugTranslation->integer == 3 ) {
			Com_Printf( "%i Loading: \"%s\"\n", count, original );
		}

		// french
		token = COM_Parse( &text_p );
		if ( Q_stricmp( "french", token ) ) {
			aborted = qtrue;
			break;
		}

		token = COM_Parse( &text_p );
		strcpy( translated[LANGUAGE_FRENCH], token );
		if ( !CL_CheckTranslationString( original, translated[LANGUAGE_FRENCH] ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: Translation formatting doesn't match up with English version!\n" );
			aborted = qtrue;
			break;
		}

		// german
		token = COM_Parse( &text_p );
		if ( Q_stricmp( "german", token ) ) {
			aborted = qtrue;
			break;
		}

		token = COM_Parse( &text_p );
		strcpy( translated[LANGUAGE_GERMAN], token );
		if ( !CL_CheckTranslationString( original, translated[LANGUAGE_GERMAN] ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: Translation formatting doesn't match up with English version!\n" );
			aborted = qtrue;
			break;
		}

		// italian
		token = COM_Parse( &text_p );
		if ( Q_stricmp( "italian", token ) ) {
			aborted = qtrue;
			break;
		}

		token = COM_Parse( &text_p );
		strcpy( translated[LANGUAGE_ITALIAN], token );
		if ( !CL_CheckTranslationString( original, translated[LANGUAGE_ITALIAN] ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: Translation formatting doesn't match up with English version!\n" );
			aborted = qtrue;
			break;
		}

		// spanish
		token = COM_Parse( &text_p );
		if ( Q_stricmp( "spanish", token ) ) {
			aborted = qtrue;
			break;
		}

		token = COM_Parse( &text_p );
		strcpy( translated[LANGUAGE_SPANISH], token );
		if ( !CL_CheckTranslationString( original, translated[LANGUAGE_SPANISH] ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: Translation formatting doesn't match up with English version!\n" );
			aborted = qtrue;
			break;
		}

		// do lookup
		t = LookupTrans( original, NULL, qtrue );

		if ( t ) {
			t->fromFile = qtrue;

			for ( i = 0; i < MAX_LANGUAGES; i++ )
				strncpy( t->translated[i], translated[i], MAX_TRANS_STRING );
		}

		token = COM_Parse( &text_p );

		// set offset if we have one
		if ( !Q_stricmp( "offset", token ) ) {
			token = COM_Parse( &text_p );
			t->x_offset = atof( token );

			token = COM_Parse( &text_p );
			t->y_offset = atof( token );

			token = COM_Parse( &text_p );
		}

		if ( Q_stricmp( "}", token ) ) {
			aborted = qtrue;
			break;
		}

		count++;
	} while ( token );

	if ( aborted ) {
		int line = 1;

		for ( i = 0; i < len && ( text + i ) < text_p; i++ ) {
			if ( text[i] == '\n' ) {
				line++;
			}
		}

		Com_Printf( S_COLOR_YELLOW "WARNING: Problem loading %s on line %i\n", fileName, line );
		cl.corruptedTranslationFile = qtrue;
	} else {
		Com_Printf( "Loaded %i translation strings from %s\n", count, fileName );
	}

	// cleanup
	free( text );
}

/*
=======================
CL_ReloadTranslation
=======================
*/
void CL_ReloadTranslation() {
	char    **fileList;
	int numFiles, i;

	for ( i = 0; i < FILE_HASH_SIZE; i++ ) {
		if ( transTable[i] ) {
			free( transTable[i] );
		}
	}

	memset( transTable, 0, sizeof( trans_t* ) * FILE_HASH_SIZE );
	CL_LoadTransTable( "scripts/translation.cfg" );

	fileList = FS_ListFiles( "translations", "cfg", &numFiles );

	for ( i = 0; i < numFiles; i++ ) {
		CL_LoadTransTable( va( "translations/%s", fileList[i] ) );
	}
}

/*
=======================
CL_InitTranslation
=======================
*/
void CL_InitTranslation() {
	char    **fileList;
	int numFiles, i;

	memset( transTable, 0, sizeof( trans_t* ) * FILE_HASH_SIZE );
	CL_LoadTransTable( "scripts/translation.cfg" );

	fileList = FS_ListFiles( "translations", ".cfg", &numFiles );

	for ( i = 0; i < numFiles; i++ ) {
		CL_LoadTransTable( va( "translations/%s", fileList[i] ) );
	}
}

#else
typedef struct trans_s {
	char original[MAX_TRANS_STRING];
	struct  trans_s *next;
	float x_offset;
	float y_offset;
} trans_t;

#endif  //DAJ USA

/*
=======================
CL_TranslateString
=======================
*/
void CL_TranslateString( const char *string, char *dest_buffer ) {
	int i, count, currentLanguage;
	trans_t *t;
	qboolean newline = qfalse;
	char *buf;

	buf = dest_buffer;
	currentLanguage = cl_language->integer - 1;

	// early bail if we only want english or bad language type
	if ( !string ) {
		strcpy( buf, "(null)" );
		return;
	} else if ( currentLanguage < 0 || currentLanguage >= MAX_LANGUAGES || !strlen( string ) )   {
		strcpy( buf, string );
		return;
	}
#if !defined( __MACOS__ )
	// ignore newlines
	if ( string[strlen( string ) - 1] == '\n' ) {
		newline = qtrue;
	}

	for ( i = 0, count = 0; string[i] != '\0'; i++ ) {
		if ( string[i] != '\n' ) {
			buf[count++] = string[i];
		}
	}
	buf[count] = '\0';

	t = LookupTrans( buf, NULL, qfalse );

	if ( t && strlen( t->translated[currentLanguage] ) ) {
		int offset = 0;

		if ( cl_debugTranslation->integer >= 1 ) {
			buf[0] = '^';
			buf[1] = '1';
			buf[2] = '[';
			offset = 3;
		}

		strcpy( buf + offset, t->translated[currentLanguage] );

		if ( cl_debugTranslation->integer >= 1 ) {
			int len2 = strlen( buf );

			buf[len2] = ']';
			buf[len2 + 1] = '^';
			buf[len2 + 2] = '7';
			buf[len2 + 3] = '\0';
		}

		if ( newline ) {
			int len2 = strlen( buf );

			buf[len2] = '\n';
			buf[len2 + 1] = '\0';
		}
	} else {
		int offset = 0;

		if ( cl_debugTranslation->integer >= 1 ) {
			buf[0] = '^';
			buf[1] = '1';
			buf[2] = '[';
			offset = 3;
		}

		strcpy( buf + offset, string );

		if ( cl_debugTranslation->integer >= 1 ) {
			int len2 = strlen( buf );
			qboolean addnewline = qfalse;

			if ( buf[len2 - 1] == '\n' ) {
				len2--;
				addnewline = qtrue;
			}

			buf[len2] = ']';
			buf[len2 + 1] = '^';
			buf[len2 + 2] = '7';
			buf[len2 + 3] = '\0';

			if ( addnewline ) {
				buf[len2 + 3] = '\n';
				buf[len2 + 4] = '\0';
			}
		}
	}
#endif //DAJ USA
}

/*
=======================
CL_TranslateStringBuf
TTimo - handy, stores in a static buf, converts \n to chr(13)
=======================
*/
const char* CL_TranslateStringBuf( const char *string ) {
	char *p;
	int i,l;
	static char buf[MAX_VA_STRING];
	CL_TranslateString( string, buf );
	while ( ( p = strstr( buf, "\\n" ) ) != NULL )
	{
		*p = '\n';
		p++;
		// Com_Memcpy(p, p+1, strlen(p) ); b0rks on win32
		l = strlen( p );
		for ( i = 0; i < l; i++ )
		{
			*p = *( p + 1 );
			p++;
		}
	}
	return buf;
}

/*
=======================
CL_OpenURLForCvar
=======================
*/
void CL_OpenURL( const char *url ) {
	if ( !url || !strlen( url ) ) {
		Com_Printf( CL_TranslateStringBuf( "invalid/empty URL\n" ) );
		return;
	}
	Sys_OpenURL( url, qtrue );
}

// Gordon: TEST TEST TEST
/*
==================
BotImport_DrawPolygon
==================
*/
void BotImport_DrawPolygon( int color, int numpoints, float* points ) {
	re.DrawDebugPolygon( color, numpoints, points );
}
