/***************************************************************************
 *   Copyright (C) 2006, IBM                                               *
 *                                                                         *
 *   Maintained by:                                                        *
 *   Eric Munson and Brad Peters                                           *
 *   munsone@us.ibm.com, bpeters@us.ibm.com                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <libvpd-2/vpdretriever.hpp>
#include <libvpd-2/component.hpp>
#include <libvpd-2/dataitem.hpp>
#include <libvpd-2/system.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // for getopt_long
#endif

#include <unistd.h>
#include <getopt.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

using namespace std;
using namespace lsvpd;

extern char *optarg;
extern int optind, opterr, optopt;

bool tabular = false, all = false, debug = false;
string device = "", path = "";

void printUsage( )
{
	string prefix( DEST_DIR );
	cout << "Usage: " << prefix;
	if( prefix[ prefix.length( ) - 1 ] != '/' )
	{
		cout << "/";
	}
	cout << "sbin/lsmcode [options]" << endl;
	cout << "options: " << endl;
	cout << " --help,       -h     print this usage message" << endl;
	cout << " --debug,      -D     print extra information about devices (sysfs locations, etc)" << endl;
	cout << " --version,    -v     print the version of vpd tools" << endl;
	cout << " --no-menus,   -c     Do not use menus.  This is the default (no menus implemented)." << endl;
	cout << " --tabular,    -r     Use a tabular format.  Overrides -c." << endl;
	cout << " --All,        -A     Display microcode level for as many devices as possible." << endl;
	cout << "                      this implies -r." << endl;
	cout << " --device=DEV, -dDEV  Only display microcode level for specified device (DEV)." << endl;
	cout << " --path=DB,    -pDB   DB is the full path to VPD db file, used instead of default." << endl;
	cout << " --zip=GZ,     -zGZ   File GZ contains database archive (overrides -d)." << endl;
}

void printVersion( )
{
	cout << "lsmcode " << VPD_VERSION << endl;
}

bool printSystem( const vector<Component*>& leaves )
{
	vector<Component*>::const_iterator i, end;
	for( i = leaves.begin( ), end = leaves.end( ); i != end; ++i )
	{
		Component* c = (*i);
		if( c->getDescription( ) == "System Firmware" )
		{
			string mi( c->getMicroCodeImage( ) );
			if( mi != "" )
			{
				if( all )
					cout << "sys0!system:";
				else
					cout << "Version of System Firmware is ";
				int pos1 = mi.find( ' ' );
				int pos2 = mi.rfind( ' ' );
				cout << mi.substr( 0, pos1 ) << " (t) ";
				cout << mi.substr( pos1 + 1, ( pos2 - pos1 ) - 1 ) << " (p) ";
				cout << mi.substr( pos2 + 1 ) << " (t)";
				if( all )
					cout << "|";
				else
					cout << endl;
			}

			vector<DataItem*>::const_iterator j, stop;
			string pfw = "";
			for( j = c->getDeviceSpecific( ).begin( ),
				stop = c->getDeviceSpecific( ).end( ); j != stop; ++j )
			{
				string val = (*j)->getValue( );
				if( val.find( "PFW" ) != string::npos )
				{
					pfw = val.substr( 4 );
					break;
				}
			}

			if( all )
				cout << "service:";
			else if( pfw != "" )
				cout << "Version of PFW is ";

			cout << pfw << endl;
			return true;
		}

		if( printSystem( c->getLeaves( ) ) )
			return true;
	}

	return false;
}

void printVPD( Component* root )
{
	if( debug )
	{
		cout << "-----------------------------" << endl;
		cout << "ID: " << root->getID( ) << endl;
		cout << "Device Tree Node: " << root->getDeviceTreeNode( ) << endl;
		cout << "Sysfs Node" << root->getSysFsNode( ) << endl;
		cout << "Sysfs Link Target: " << root->getSysFsLinkTarget( ) << endl;
		cout << "HAL UDI: " << root->getHalUDI( ) << endl;
		cout << "Sysfs Device Class Node: " << root->getDevClass( ) << endl;
		cout << "Parent ID: " << root->getParent( ) << endl;
		cout << "Sysfs Name: " << root->getDevSysName( ) << endl;
		cout << "Device Tree Name: " << root->getDevTreeName( ) << endl;
		cout << "Bus: " << root->getDevBus( ) << endl;
		cout << "Device Type: " << root->getDevType( ) << endl;
		cout << "Bus Address: " << root->getDevBusAddr( ) << endl;
		cout << "Loc Code: " << root->getPhysicalLocation( ) << endl;
		cout << "Second Location: " << root->getSecondLocation( ) << endl;

		vector<string>::const_iterator i, end;
		for( i = root->getChildren( ).begin( ), end = root->getChildren( ).end( );
			i != end; ++i )
		{
			cout << *(i) << endl;
		}
	}

	if( root->getFirmwareVersion( ) != "" )
	{
		vector<DataItem*>::const_iterator j, stop;
		for( j = root->getAIXNames( ).begin( ),
			stop = root->getAIXNames( ).end( ); j != stop; ++j )
		{
			cout << (*j)->getValue( ) << " ";
		}
		cout << "!" << root->getModel( );
		cout << "." << root->getFirmwareVersion( ) << endl;
	}
	else if( root->getFirmwareLevel( ) != "" )
	{
		vector<DataItem*>::const_iterator j, stop;
		for( j = root->getAIXNames( ).begin( ),
			stop = root->getAIXNames( ).end( ); j != stop; ++j )
		{
			cout << (*j)->getValue( ) << " ";
		}
		cout << "!" << root->getModel( );
		cout << "." << root->getFirmwareLevel( ) << endl;
	}

	const vector<Component*> children = root->getLeaves( );
	vector<Component*>::const_iterator i, end;
	for( i = children.begin( ), end = children.end( ); i != end; ++i )
	{
		printVPD( *i );
	}
}

void printVPD( System* root )
{
	if( debug )
	{
		cout << "ID: " << root->getID( ) << endl;
		cout << "Device Tree Node: " << root->getDevTreeNode( ) << endl;
		vector<string>::const_iterator i, end;
		cout << "Children: " << endl;
		for( i = root->getChildren( ).begin( ), end = root->getChildren( ).end( );
			i != end; ++i )
		{
			cout << *(i) << endl;
		}
		cout << "-----------------------------" << endl;
	}

	printSystem( root->getLeaves( ) );

	if( !all )
		return;

	const vector<Component*> children = root->getLeaves( );
	vector<Component*>::const_iterator i, end;
	for( i = children.begin( ), end = children.end( ); i != end; ++i )
	{
		printVPD( *i );
	}
}

int main( int argc, char** argv )
{
	char opts [] = "hvDcrAd:p:z:";
	bool done = false;
	bool compressed = false;
	System * root = NULL;
	VpdRetriever* vpd = NULL;
	int index;

	struct option longOpts [] =
	{
		{ "help", 0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ "no-menus", 0, 0, 'c' },
		{ "tabular", 0, 0, 'r' },
		{ "All", 0, 0, 'A' },
		{ "device", 1, 0, 'd' },
		{ "path", 1, 0, 'p' },
		{ "zip", 1, 0, 'z' },
		{ "debug", 0, 0, 'D' },
		{ 0, 0, 0, 0 }
	};

	if (geteuid() != 0) {
		cout << "Must be run as root!" << endl;
		return -1;
	}

	while( !done )
	{
		switch( getopt_long( argc, argv, opts, longOpts, &index ) )
		{
			case 'c':
				break;

			case 'v':
				printVersion( );
				return 0;

			case 'r':
				tabular = true;
				break;

			case 'A':
				all = true;
				tabular = true;
				break;

			case 'D':
				debug = true;
				break;

			case 'd':
				device = optarg;
				break;

			case 'p':
				if( path == "" )
					path = optarg;
				break;

			case 'z':
				path = optarg;
				compressed = true;
				break;

			case -1:
				done = true;
				break;

			case 'h':
			default:
				printUsage( );
				return 0;
		}
	}

	if( path != "" )
	{
		string env, db;
		int index;

		if( compressed )
		{
			gzFile gzf = gzopen( path.c_str( ), "rb" );
			if( gzf == NULL )
			{
				cout << "Faile to open database archive " << path << endl;
				return 1;
			}

			index = path.rfind( '.' );
			path = path.substr( 0, index );
			int fd = open( path.c_str( ), O_CREAT | O_WRONLY,
				S_IRGRP | S_IWUSR | S_IRUSR | S_IROTH );
			if( fd < 0 )
			{
				cout << "Failed to open file for uncompressed database archive"
					<< endl;
					return 1;
			}

			char buffer[ 4096 ] = { 0 };

			int in = 0;

			while( ( in = gzread( gzf, buffer, 4096 ) ) > 0 )
			{
				int out = 0, tot = 0;
				while( ( out = write( fd, buffer + tot, in - tot ) ) > 0 &&
					tot < in )
					tot += out;
				memset( buffer, 0 , 4096 );
			}
			close( fd );

			if( gzclose( gzf ) != 0 )
			{
				int err;
				cout << "Error reading archive " << path << ".gz: " <<
					gzerror( gzf, &err ) << endl;
				return 1;
			}
		}

		index = path.rfind( "/" );
		env = path.substr( 0, index + 1 );
		db = path.substr( index + 1 );
		try
		{
			vpd = new VpdRetriever( env, db );
		}
		catch( exception& e )
		{
			string prefix( DEST_DIR );
			cout << "Please run " << prefix;
			if( prefix[ prefix.length( ) - 1 ] != '/' )
			{
				cout << "/";
			}
			cout << "sbin/vpdupdater before running lsmcode." << endl;
			return 1;
		}
	}
	else
	{
		try
		{
			vpd = new VpdRetriever( );
		}
		catch( exception& e )
		{
			string prefix( DEST_DIR );
			cout << "Please run " << prefix;
			if( prefix[ prefix.length( ) - 1 ] != '/' )
			{
				cout << "/";
			}
			cout << "sbin/vpdupdate before running lsmcode." << endl;
			return 1;
		}
	}

	if( vpd != NULL )
	{
		try
		{
			root = vpd->getComponentTree( );
		}
		catch( VpdException& ve )
		{
			cout << "Error reading VPD DB: " << ve.what( ) << endl;
			delete vpd;
			return 1;
		}

		delete vpd;
	}

	if( root != NULL )
	{
		printVPD( root );
		delete root;
	}

	if( compressed )
	{
		unlink( path.c_str( ) );
	}

	return 0;
}
