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
#ifdef HAVE_CONFIG_H
//Get all automake #define's
#include <config.h>
#endif

#include <devicetreecollector.hpp>
#include <rtascollector.hpp>

#include <libvpd-2/logger.hpp>
#include <libvpd-2/lsvpd_error_codes.hpp>
#include <libvpd-2/lsvpd.hpp>
#include <libvpd-2/helper_functions.hpp>
#include <libvpd-2/Source.hpp>
#include <libvpd-2/vpdexception.hpp>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sstream>

using namespace std;

namespace lsvpd
{
	DeviceTreeCollector::DeviceTreeCollector( )
	{
	}

	DeviceTreeCollector::~DeviceTreeCollector( )
	{
	}

	/**
	 * Internal: For DeviceTreeCollector, init will test to see if
	 * the PowerPC specifc information (specifically /proc/device-tree)
	 * is available.
	 */
	bool DeviceTreeCollector::setup(const string path_t )
	{
		struct stat statbuf;
		bool ret;

		if (stat(path_t.c_str(), &statbuf) != 0)
			ret = false;
		else {
			rootDir = path_t;
			fsw = FSWalk(rootDir);
			ret = true;
		}

		return ret;
	}

	bool DeviceTreeCollector::init()
	{
		return setup("/proc/device-tree");
	}

	/**
	 * Uses the highest preference Source to fill this DataItem
	 */
	void DeviceTreeCollector::readSources( DataItem& di,
		const string& devTreeNode )
	{
		const Source * s = di.getFirstSource( );
		if( s != NULL && s ->getType( ) == SRC_DEVICETREE )
		{
			string val = getAttrValue( devTreeNode, s->getData( ) );
			if( val != "" )
				di.setValue( val, s->getPrefLvl( ), __FILE__, __LINE__ );
		}
		return;
	}

	void DeviceTreeCollector::initComponent( Component * newComp )
	{
			Source *src = new Source("ibm,fw-adapter-name", "",
				 SRC_DEVICETREE, ASCII, 1, 3);
			newComp->mDescription.setHumanName("Description");
			newComp->mDescription.setAC("DS");
			newComp->mDescription.addSource(src);
	}

	Component* DeviceTreeCollector::fillComponent(Component* fillMe )
	{
		if( fillMe->deviceTreeNode.dataValue == "" )
		{
			return fillMe;
		}

		initComponent(fillMe);

		fillDS( fillMe );

		fillIBMVpd( fillMe );

		return fillMe;
	}

	/**
	 * If no parameter exists with code=code1, try to get the one
	 * with code=code2.
	 */
	string collectEitherSystemParm(int code1,
		int code2)
	{

		string s = RtasCollector::rtasSystemParm(code1);
		if (s.empty()) {
			s = RtasCollector::rtasSystemParm(code2);
		}
		return s;
	}

	void DeviceTreeCollector::addSystemParms(Component *c)
	{
		string s = collectEitherSystemParm(37, 18);
		if (!s.empty()) {
			c->n5.setValue(s, INIT_PREF_LEVEL, __FILE__, __LINE__);
		}

		s = collectEitherSystemParm(38, 19);
		if (!s.empty()) {
			c->n6.setValue(s, INIT_PREF_LEVEL, __FILE__, __LINE__);
		}
	}

	/**
	 * Take the RTAS VPD collected from the rtasGetVPD call, parse the
	 * output for new Components, fill the new Components with the VPD
	 * provided and add the new Components to the Component vector.
	 */
	void DeviceTreeCollector::parseRtasVpd( vector<Component*>& devs,
		char *rtasData, int rtasDataSize)
	{
		unsigned int size = 0;
		unsigned int ret = 1;

		if( rtasData != NULL && rtasDataSize > 0 ) {
			while( size < rtasDataSize && ret > 0 )
			{
				Component* c = new Component( );
				ret = parseVPDBuffer( c, rtasData );
				if( ret == 0 )
				{
					delete c;
					return;
				}

				rtasData += ret;
				size += ret;

				if( c->getDescription( ) == "System VPD" )
				{
					delete c;
					continue;
				}

				/*
				 * Build a uniquie device ID and deviceTreeNode
				 */
				ostringstream os;
				os << "/proc/device-tree/rtas/"  << c->getPhysicalLocation( );

				c->idNode.setValue( os.str( ), 100, __FILE__, __LINE__ );
				c->deviceTreeNode.setValue( os.str( ), 100, __FILE__, __LINE__ );
				c->mParent.setValue( "/sys/devices", 1, __FILE__, __LINE__ );

				devs.push_back( c );
			}
		}
	}

	bool DeviceTreeCollector::fillQuickVPD(Component * fillMe)
	{
		string val, path, path_tmp;
		int done = 0, i;

		/* Grab an easy AIX Name, higher quality than the base AIX */
		val = getAttrValue( fillMe->deviceTreeNode.dataValue, "name" );
		if (val.length() > 0)
			fillMe->addAIXName(val, INIT_PREF_LEVEL + 1);

		/* Fill TM Values */
		val = getAttrValue( fillMe->deviceTreeNode.dataValue, "model" );
		if (val.length() > 0)
			fillMe->mModel.setValue( val, 20, __FILE__, __LINE__ );

		val = getAttrValue( fillMe->deviceTreeNode.dataValue, "device_type" );
		if (val.length() > 0)
			fillMe->mModel.setValue( val, 21, __FILE__, __LINE__ );

		/* Loc code */
		/* bpeters: Many storage devices sit on a port of an adapter, and are not
		 * themselves given loc-code files by firmware.  Thus, the general rule to
		 * finding a loc-code is to start at the node the device sits at, then if
		 * not found, back up and up until found.
		 */
		path = fillMe->deviceTreeNode.dataValue;
		val = getAttrValue( path, "ibm,loc-code" );
		if (val.length() > 0) {
			fillMe->mPhysicalLocation.setValue( val, 90, __FILE__, __LINE__ );
			return true;
		}
		else { /* walk up device path until loc-code found */
			i = path.find_last_of("/", path.length() - 1);
			while (i != -1) {
				path_tmp = path;
				path = path_tmp.substr(0, i);
				val = getAttrValue( path, "ibm,loc-code" );
				if (val.length() > 0) {
					fillMe->mPhysicalLocation.setValue( val, 90, __FILE__, __LINE__ );
					return true;
				}

				i = path.find_last_of("/", path.length() - 1);
			}
		}

		return false;
	}

	void DeviceTreeCollector::fillDS( Component* fillMe )
	{
		string node;
		string val;
		int loc = 0;
		node = fillMe->getDevTreeNode( );
		loc = node.rfind( '/' );

		node = node.substr( loc +1 );
		loc = node.find( '@' );
		val = node.substr( 0, loc );
		if( val == "pci" )
			val = "PCI Bus";
		else if( val == "ide" )
			val = "IDE Controller";
		fillMe->mDescription.setValue( val, 25, __FILE__, __LINE__ );

		val = getAttrValue( fillMe->deviceTreeNode.dataValue,
			 "ibm,fw-adapter-name" );
		if( val != "" )
		{
			ostringstream os;
			os.str( fillMe->deviceTreeNode.dataValue );
			os << "/wide";
			struct stat info;
			if( stat( os.str( ).c_str( ), &info ) == 0 )
			{
				os.str( "Wide/" );
				os << val;
				val = os.str( );
			}
			fillMe->mDescription.setValue( val, 60, __FILE__, __LINE__ );
		}
	}

	/**
	 * Grab the contents of the ibm,vpd file
	 */
	void DeviceTreeCollector::fillIBMVpd( Component* fillMe )
	{
		char* buf, *start;
		struct stat info;
		ostringstream os;
		int fd, input = 0;

		os << fillMe->deviceTreeNode.dataValue << "/ibm,vpd";
		fd = open( os.str( ).c_str( ), O_RDONLY );
		if( fd < 0 )
		{
			return;
		}

		fstat( fd, &info );
		buf = new char[ info.st_size + 1 ];
		if( buf == NULL )
		{
			close( fd );
			return;
		}
		start = buf;

		memset( buf, '\0', info.st_size + 1 );

		int ret;
		while( input < info.st_size &&
				( ret = read( fd, buf + input, info.st_size - input ) >= 0 ) )
		{
			input += ret;
		}
		close( fd );

		if( info.st_size != ( *((int*)buf) + 4 ) )
		{
			Logger logger;
			ostringstream msg;
			msg << "VPD file size different from reported size, VPD ";
			msg << "retireval will continue but the values maybe corrupted.  ";
			msg << "Filesystem size = ";
			msg << info.st_size << " reported size = " << ( *((int*)buf) + 4 );
			logger.log( msg.str( ), LOG_WARNING );
		}

		parseVPDBuffer( fillMe, buf );

		delete [] start;
	}

	/**
	 * Parse VPD out of the VPD buffer and pass key/value pairs into
	 * setVPDField
	 */
	unsigned int DeviceTreeCollector::parseVPDBuffer( Component* fillMe,
		char * buf )
	{
		u32 size;
		char key[ 3 ] = { '\0' };
		char val[ 256 ]; // Each VPD field will be at most 255 bytes long
		unsigned char length;
		unsigned char type;
		char * recordStart;

		memcpy( &size, buf, 4 );
		buf += 4;
		recordStart = buf;

		type = *buf;
		buf++;

		if( type == 0x82 )
		{
			unsigned int i;
			/*
			 * VPD with FRU namename, Large Resource type VPD keywords tag
			 * The length is 4 bytes big endian...
			 */
			i = *buf;
			buf++;
			i += ((*buf) << 8);
			buf++;
			if( buf > recordStart + size )
			{
				goto ERROR;
			}

			memset( val, '\0', 256 );
			memcpy( val, buf, i );
			buf += i;

			fillMe->mDescription.setValue( string( val ), 80, __FILE__, __LINE__ );

			buf += 3;
		}

		/*
		 * Parsing the vpd file is the same from here on out regardless of the
		 * flag checked earlier.
		 */
		while( buf < recordStart + size && *buf != 0x78 && *buf != 0x79 )
		{
			memset( val, '\0', 256 );
			memset( key, '\0', 3 );

			if( buf + 3 > recordStart + size )
			{
				goto ERROR;
			}

			key[ 0 ] = buf[ 0 ];
			key[ 1 ] = buf[ 1 ];
			length = buf[ 2 ];
			buf += 3;

			if( buf + length > recordStart + size )
			{
				goto ERROR;
			}

			memcpy( val, buf, length );
			buf += length;

			setVPDField( fillMe, key, val, __FILE__, __LINE__ );
		}
		return size + 4;
ERROR:
		Logger logger;
		logger.log( "Attempting to parse corrupt VPD buffer.", LOG_WARNING );
		return 0;
	}

	bool isDevice(string str)
	{
		bool ok = true;
		int i;

		i = str.length();
		while (ok) {
			switch (str[i]) {
				case '\0':;
				case '0':;
				case '1':;
				case '2':;
				case '3':;
				case '4':;
				case '5':;
				case '6':;
				case '7':;
				case '8':;
				case '9':;
				case 'a':;
				case 'b':;
				case 'c':;
				case 'd':;
				case 'e':;
				case 'f':;
				case 'g':;
				case 'h':;
				case 'i':;
				case 'j':;
				case 'k':;
				case 'l':;
				case 'm':;
				case 'n':;
				case 'o':;
				case 'p':;
				case 'q':;
				case 'r':;
				case 's':;
				case 't':;
				case 'u':;
				case 'v':;
				case 'w':;
				case 'x':;
				case 'y':;
				case 'z':;
				case ',':;
				case ':':
							i--;
							break;
				default : ok = false;
			}
		}
		if (str[i] == '@')
			return true;
		else
			return false;

	}

	/**
	 * Walks a vector of component ptr's, looking for a particular one
	 * based on the deviceTreeNode
	 * @return the specified component, or NULL on failure
	 */
	Component *DeviceTreeCollector::findComponent(
		const vector<Component*> devs, string devPath )
	{
		for (int i = 0; i < devs.size(); i++)
			if (devs[i]->deviceTreeNode.getValue() == devPath)
				return devs[i];

		return NULL;
	}

	/**
	 * Walkes a vector of component ptr's, looking for a particular one
	 * --> based on the idNode
	 * @return the specified component, or NULL on failure
	 */
	Component *DeviceTreeCollector::findCompIdNode(
		vector<Component*> *devs, const string id )
	{
		vector<Component*>::iterator i, end;

		for ( i = devs->begin( ), end = devs->end( ); i != end; ++i )
			if ((*i)->idNode.getValue() == id)
				return *i;

		return NULL;
	}

	/**
	 * if device is represented in both /sys and device-tree, we need to
	 * merge the two components
	 * @args: dest = sysFS component
	 * @args: src = device-tree component
	 */
	void DeviceTreeCollector::cpyinto(Component *dest, Component *src)
	{
		vector<Component*>::iterator it, end;
		vector<string> kids = src->getChildren( );
		vector<string>::iterator vi, vend;

		dest->deviceTreeNode.setValue(src->deviceTreeNode.getValue(), 100,
												__FILE__, __LINE__);
		dest->devDevTreeName.setValue(src->devDevTreeName.getValue(), 100,
												__FILE__, __LINE__);
		dest->devType.setValue(src->devType.getValue(),
			src->devType.getPrefLevelUsed(), __FILE__, __LINE__);

		/* Copy children */
		for ( vi = kids.begin( ), vend = kids.end( ); vi != vend; ++vi ) {
			dest->addChild(*vi);
		}

	}

	/*
	 * transferChildren
	 * @brief When a component is discovered in device tree collector,
	 * 	then moved into the full device vector, this method handles
	 * 	moving over the children (recursively) of the moved component
	 * @arg parentSrc - The parent component from the src tree
	 * @arg defaultParent - The parent of top level components,
	 * 	in this case, defined as those who's parents are
	 * 	"/proc/device-tree"
	 * @arg target - The collection into which the children should
	 * 	be moved
	 * @arg src - Collection from which the children are being moved.
	 * 	Note, this collection is unchanged, components are simply
	 * 	copied.
	 */

	 /* transferChildren(&devRoot, sysRoot, &sysdevs, &devs); */
	void DeviceTreeCollector::transferChildren(Component *current,
			vector<Component*> *target,
			vector<Component*> *src)
	{
		vector<string> kids = current->getChildren( );
		vector<string>::iterator vi, vend;
		vector<Component*>::iterator i, end;

		for ( vi = kids.begin( ), vend = kids.end( ); vi != vend; ++vi )
			for ( i = src->begin( ), end = src->end( ); i != end; ++i ) {
				if ( (*i)->idNode.getValue() == *vi) {
					target->push_back(*i);
//					src->erase(i);
					/*Recurse*/
					transferChildren(*i, target, src);
				}
			}

	}

	void DeviceTreeCollector::mergeTrees(Component *current,
			Component *defaultParent,
			vector<Component*> *target,
			vector<Component*> *src)
	{
		vector<string> kids = current->getChildren( );
		vector<string>::iterator vi, vend;
		vector<Component*>::iterator i, end;
		Component *child, *parent;

		for ( vi = kids.begin( ), vend = kids.end( ); vi != vend; ++vi ) {
			child = findCompIdNode(src, *vi);
			if (child != NULL) {
				mergeTrees(child, defaultParent, target, src);

				if (child->devState == COMPONENT_STATE_DEAD) {
					current->removeChild(child->idNode.dataValue);
				}
			}
		}

		/* At node which needs to be merged into existing node in /sys */
		if (current->devState == COMPONENT_STATE_DEAD) {
			/* transfer all children of this node to new tree */
			transferChildren(current, target, src);

			cpyinto(current->devSelfInSysfs, current);
			parent = findCompIdNode(target, current->devSelfInSysfs->mParent.dataValue);
			/* Update childs mParent pointer -
			 * Note: The parent does not require a child ptr, since merged
			 *  and already present */

			current->mParent.setValue(
					parent->idNode.dataValue,
					90, __FILE__, __LINE__);

		}
		/* Convert top-level devices to point to defaultParent*/
		else if (current->mParent.dataValue == DEVTREEPATH) {
			current->mParent.setValue(defaultParent->idNode.dataValue,
				 80, __FILE__, __LINE__);
			defaultParent->addChild(current->idNode.dataValue);
			target->push_back(current);
			/* Recursively transfer all children of this node to new tree */
			transferChildren(current, target, src);
		}
	}

	/* @brief Determine the parent of this device, using a bus-specific
	 * 	search mechanism.  SCSI specific at this point, but some of this
	 * 	may be generalizable and expanded for application to usb and ide
	 * 	buses in future versions
	 *
	 * @param devs - /sys & /proc/device-tree Merged tree of devices
	 */
	Component * DeviceTreeCollector::findSCSIParent(Component *fillMe,
										vector<Component*> devs)
	{
		string sysDev, devPath;
		string parentSysFsName, pYL;
		vector<Component*>::iterator i, end;
		const DataItem *devSpecific;
		int loc;

		devSpecific = fillMe->getDeviceSpecific("XB");
		if (devSpecific != NULL) {
			parentSysFsName = fillMe->devBus.dataValue
									+ string("@")
									+ devSpecific->dataValue;

			sysDev = fillMe->sysFsLinkTarget.dataValue;
			while (sysDev.length() > 1) {
				if ((loc = sysDev.rfind("/", sysDev.length())) != string::npos )
					sysDev = sysDev.substr(0, loc);

				for( i = devs.begin( ), end = devs.end( ); i != end; ++i ) {
					if (((*i)->sysFsLinkTarget.dataValue != "")) {
						if ((*i)->sysFsLinkTarget.dataValue == sysDev)
							return (*i);
					}
				}
			}
		}
		return NULL;
	}

	/* @brief Collect Location Code information for devices that do not
	 * give us a nice ibm,loc-code
	 * @arg devs - device tree discovered devices
	 *
	 * TODO: Need to move Bus parsing for devices to before this step,
	 * 	so we can test if bus == scsi, ide, usb, and ONLY do this extra
	 * 	step for these device types
	 */
	void DeviceTreeCollector::buildSCSILocCode(Component *fillMe,
											vector<Component*> devs)
	{
		Component *parent;
		ostringstream val;
		const DataItem *target, *lun, *bus;

		/* Build up a distinct YL based on parents YL - for device such as
		 * 	scsi, ide, usb, etc that do not generate ibm,loc-code
		 * 	files for easy grabbing
		 */
		if (fillMe->devBus.dataValue == "scsi") {
			parent = findSCSIParent(fillMe, devs);

			if (parent != NULL) {
				target = fillMe->getDeviceSpecific("XT");
				lun = fillMe->getDeviceSpecific("XL");
				bus = fillMe->getDeviceSpecific("XB");
				if (target != NULL && lun != NULL && bus != NULL) {
					if (fillMe->mPhysicalLocation.dataValue != "")
						val << fillMe->mPhysicalLocation.dataValue;
					else if
					(parent->mPhysicalLocation.dataValue != "")
						val << parent->mPhysicalLocation.dataValue;
					else
						val << getAttrValue( parent->deviceTreeNode.dataValue,
											 "ibm,loc-code" );
					val << "-B" << bus->dataValue << "-T" << target->dataValue
						<< "-L" << lun->dataValue;
					fillMe->mPhysicalLocation.setValue( val.str( ), 60 ,
						__FILE__, __LINE__ );
				}
			}
		}
	}

	/**
	 * Creates a vector of components, representing all devices this collector
	 * is aware of,	(vector should be output from getComponents),
	 * and back-walks tree to determine inheritance
	 *
	 * @arg sysdevs: Devices discovered through sysfstreecollector
	 * @return: A fully merged tree of devices, with a single
	 * 		root device and some path of inheritance to all
	 * 		children
	 */
	vector<Component*> DeviceTreeCollector::getComponents(
		vector<Component*>& sysdevs )
	{
		vector<Component*> devs;
		vector<Component*>::iterator i, end;
		Component *child, *parent, *devC, *sysRoot;
		Component devRoot;
		string devPath;
		int bufSize;
		char *rtasData = NULL;

		// Discover all devices
		getComponentsVector( devs );


		bufSize = RtasCollector::rtasGetVPD("", &rtasData);

		if( bufSize < 0 )
		{
			Logger logger;
			logger.log( interp_err_code(bufSize), LOG_WARNING );
		}

		parseRtasVpd( devs, rtasData, bufSize);
		delete rtasData;

		/* Grab system params from rtas, N5 and N6 */
		Component* c = new Component( );
		/*
		 * Build a unique device ID and deviceTreeNode for the system params
		 */
		ostringstream os;
		os << "/proc/device-tree/rtas/";
		c->idNode.setValue( os.str( ), 100, __FILE__, __LINE__ );
		c->deviceTreeNode.setValue( os.str( ), 100, __FILE__, __LINE__ );
		c->mParent.setValue( "/sys/devices", 1, __FILE__, __LINE__ );
		addSystemParms( c );

		devs.push_back( c );


		 devRoot.idNode.setValue(DEVTREEPATH, 100,
													__FILE__, __LINE__);
		// Determine tree structure
		for( i = devs.begin( ), end = devs.end( ); i != end; ++i ) {
			child = *i;
			devPath = child->deviceTreeNode.dataValue;
			parent = NULL;
			while (HelperFunctions::dropDir(devPath) && (devPath != DEVTREEPATH)) {
				parent = findComponent(devs, devPath);

				if (NULL != parent) {
					child->mParent.setValue(parent->idNode.dataValue, 80,
													__FILE__, __LINE__);
					parent->addChild(child->idNode.dataValue);
					break;
				}
			}

			if (parent == NULL) {
				child->mParent.setValue(DEVTREEPATH, 60,
													__FILE__, __LINE__);
				devRoot.addChild(child->idNode.dataValue);
			}
		}

		/* Merge the two trees */
		sysRoot = findCompIdNode(&sysdevs, "/sys/devices");

		for ( i = sysdevs.begin( ), end = sysdevs.end( ); i != end; ++i ) {
			if ((*i)->deviceTreeNode.dataValue.length() > 0) {
				Component *devComp = findCompIdNode(&devs, (*i)->deviceTreeNode.dataValue);
				if (devComp != NULL) {
					/* mark devComp as 'dead' - it should not be moved into
					 * target tree during inheritance shuffle, as it already
					 * exists in some form */
					devComp->devState = COMPONENT_STATE_DEAD;
					devComp->devSelfInSysfs = *i;
				}
			}
		}

		/* Now merge these two trees */
		mergeTrees(&devRoot, sysRoot, &sysdevs, &devs);

		/* Get Location Codes for device
		 * Loop thru each device and build up YL field.  If YL not easily
		 * obtained, build it for certain buses
		 */
		for( i = sysdevs.begin( ), end = sysdevs.end( ); i != end; ++i ) {
			devC = *i;

			//Deprecated behavior: Get Yl from ibm,loc-code if possible
			/* bpeters: basic loc-code discovery now iterates up the device path,
			 * so parent loc-codes are always going to be obtained if present
			 */
			fillQuickVPD( devC );
				// Deprecated behavior: Otherwise, build one from parent
			if (devC->devBus.dataValue == "scsi") {
				buildSCSILocCode(devC, sysdevs);
			}
		}

		return sysdevs;
	}

	/* Walks tree, ID's all devices, adds path and other relevant data
	 * to list list.  Must call fillComponent on these to obtain full
	 * details
	 */
	vector<Component*> DeviceTreeCollector::getComponentsVector(
		vector<Component*>& devs )
	{
		vector<string> curList;
		vector<string> fullList;
		int processed = 0;
		int fullListFrontNode = 0;
		string curPath, tmpDirName, devType;
		Component *tmp;
		FSWalk fsw = FSWalk();
		bool dev;

		fullList.push_back(rootDir); //Start at top of device tree

		while ((fullList.size() - fullListFrontNode) > 0 ) {

			curPath = fullList[fullListFrontNode];
			fsw.fs_getDirContents(curPath, 'd', curList);

			while (curList.size() > 0) {
				tmpDirName = curList.back();
				dev = false;

				if (isDevice(tmpDirName)) {
					// Create new component
					tmp = new Component();
					/*
					 * Set identifying node (idNode) to dev-tree entry if
					 * sysfs did not previously set it *
					 */
					if (tmp->deviceTreeNode.dataValue.length() == 0) {
						tmp->idNode.setValue(curPath  + "/" + tmpDirName,
							INIT_PREF_LEVEL - 1, __FILE__, __LINE__);
					}

					tmp->deviceTreeNode.setValue(curPath  + "/" + tmpDirName,
						INIT_PREF_LEVEL, __FILE__, __LINE__);

					// Set what we know of name - ie - directory entry
					// Set immediately available data fields
					tmp->devDevTreeName.setValue(tmpDirName, INIT_PREF_LEVEL,
															__FILE__, __LINE__);

					devType = getAttrValue(tmp->idNode.dataValue,
						"device_type");
					tmp->devType.setValue(devType, 60, __FILE__, __LINE__);
					/*
					 * Do not add if this is a memory device. These are
					 * discovered through rtas
					 */
					if (tmp->devDevTreeName.dataValue.substr(0, 6) !=
						"memory")
						devs.push_back(tmp);

					/* default parent is top object.
					 * This will be overwritten as more detailed info becomes
					 * available */
					tmp->mParent.setValue("/proc/device-tree",
						 INIT_PREF_LEVEL, __FILE__, __LINE__);

					dev = true;
				}
				// Push all dirs into fullList for future walking
				fullList.push_back(curPath + "/" + tmpDirName);

				curList.pop_back();
			}
		fullListFrontNode++;
			processed++;
		}

		return devs;
	}

	int DeviceTreeCollector::numDevicesInTree(void)
	{
		int numDevs = 0;
		vector<string> curList;
		vector<string> fullList;
		int fullListFrontNode = 0;
		string curPath, tmpDirName;
		FSWalk fsw = FSWalk();

		fullList.push_back(rootDir); //Start at top of device tree

		while ((fullList.size() - fullListFrontNode) > 0 ) {

			curPath = fullList[fullListFrontNode];
			fsw.fs_getDirContents(curPath, 'd', curList);

			while (curList.size() > 0) {
				if (isDevice(curList.back()))
					numDevs++;

				/* Push all dirs into fullList for future walking */
				fullList.push_back(curPath + "/" + tmpDirName);
				curList.pop_back();
			}
			fullListFrontNode++;
		}

		return numDevs;
	}

	string DeviceTreeCollector::myName(void)
	{
		return string("DeviceTreeCollector");
	}

	/* Parses rtas and various system files for system level VPD
	 */
	void DeviceTreeCollector::fillSystem( System* sys )
	{
		char* rtasVPD = NULL;
		int size;

		string val = "";

		sys->deviceTreeNode.setValue( "/proc/device-tree", 100
												, __FILE__, __LINE__);

		sys->mKeywordVersion.setValue( "ipzSeries", 10, __FILE__, __LINE__ );

		val = getAttrValue( "/proc/device-tree", "device_type" );
		if( val != "" )
			sys->mArch.setValue( val, 100, __FILE__, __LINE__ );

		val = getAttrValue( "/proc/device-tree", "model" );
		if( val != "" )
		{
			sys->mMachineType.setValue( val, 80, __FILE__, __LINE__ );
			sys->mMachineModel.setValue( val.substr( 4 ), 80, __FILE__, __LINE__);
		}

		val = getAttrValue("/proc/device-tree", "system-id" );
		if( val != "" )
		{
			sys->mSerialNum1.setValue( val, 80, __FILE__, __LINE__ );
			sys->mProcessorID.setValue( val, 80, __FILE__, __LINE__ );
			if( val.length( ) > 6 )
				sys->mSerialNum2.setValue( val.substr( 6 ), 80, __FILE__, __LINE__ );
		}

		int pos;
		val = sys->mMachineModel.dataValue;
		if( val == "" )
			return;
		ostringstream os;
		os << "U";
		while( ( pos = val.find( "-" ) ) != string::npos )
		{
			val[ pos ] = '.';
		}

		os << val << "." << sys->getSerial2( );
		sys->mLocationCode.setValue( os.str( ), 100, __FILE__, __LINE__ );

		size = RtasCollector::rtasGetVPD( os.str( ), &rtasVPD );
		if( rtasVPD != NULL && size > 0 )
		{
			parseSysRtas( rtasVPD, sys );
		}

	}

	void DeviceTreeCollector::parseSysRtas( char * data, System* sys )
	{
		u32 size = 0;
		unsigned char type = 0, recordSize = 0;
		char key[ 3 ] = { '\0' };
		char val[ 256 ] = { '\0' };
		char* recordStart;

		memcpy( &size, data, 4 );
		data += 4;
		recordStart = data;

		type = data[ 0 ];
		data++;
		recordSize = data[ 0 ];
		data += 2;

		if( type == 0x82 )
		{
			memcpy( val, data, recordSize );
			data += recordSize;
			sys->mDescription.setValue( val, 60, __FILE__, __LINE__ );
			memset( val, 0, 256 );
		}

		while( data[ 0 ] != 0x78 && data < recordStart + size )
		{
			key[ 0 ] = data[ 0 ];
			key[ 1 ] = data[ 1 ];
			recordSize = data[ 2 ];
			data += 3;
			memset( val, 0, 256 );
			memcpy( val, data, recordSize );
			setVPDField( sys, key, val, __FILE__, __LINE__ );
			data += recordSize;
		}
	}

	void DeviceTreeCollector::parseMajorMinor( Component* comp, string& major,
		string& minor )
	{
		string addr = comp->devSysName.dataValue;
		int index;

		if( comp->devBus.dataValue == "scsi" )
		{
			index = addr.find( ':' );
			if (index == string::npos)
				return;
			major = addr.substr( 0, index );
			index = addr.rfind( ':' );
			minor = addr.substr( index + 1 );
		}
		else if( comp->devBus.dataValue == "ide" )
		{
			index = addr.find( '.' );
			if (index == string::npos)
				return;
			major = addr.substr( 0, index );
			minor = addr.substr( index + 1 );
		}
		else if( comp->devBus.dataValue == "usb" &&
			( index = addr.find( ':' ) ) != string::npos )
		{
			major = addr.substr( 0, index );
			minor = addr.substr( index + 1 );
		}
	}

	string DeviceTreeCollector::getBaseLoc( Component* comp )
	{
		string loc = comp->mPhysicalLocation.dataValue;
		if( loc != "" && loc[ 0 ] == 'U' )
			return loc;

		if( comp->mpParent == NULL )
			return "";

		return getBaseLoc( comp->mpParent );
	}

	bool DeviceTreeCollector::checkLocation( Component* comp )
	{
		ostringstream os;
		if( comp->mPhysicalLocation.dataValue != "" &&
			comp->mPhysicalLocation.dataValue[ 0 ] == 'U' )
			return true;

		/*
		 * This location code is not as descriptive as it could be, build a
		 * better one
		 */

		 if( comp->mpParent == NULL )
		 	return false;

		string baseLoc = getBaseLoc( comp->mpParent );

		if (baseLoc == "")
			return false;

		/*
		 * If the Location code of our parent was not updated when
		 * we filled our Location code, prefix the Location code
		 * now to the existing code.
		 */
		if (comp->mPhysicalLocation.dataValue != "" &&
			comp->mPhysicalLocation.dataValue[ 0 ] == '-') {
			os << baseLoc << comp->mPhysicalLocation.getValue();
			comp->mPhysicalLocation.setValue( os.str( ), 90, __FILE__,
				__LINE__ );
			return true;
		}

		string major = "", minor = "";

		parseMajorMinor( comp, major, minor );

		/*
		 * Major number 0 is not allocated in Linux.
		 * Just use the loc code of our parent in that case.
		 */
		os << baseLoc;
		if (major != "" && major != "0") {
			os << "-L" << major;
			if( minor != "" && minor != "0" )
				os << "-L" << minor;
			return true;
		}

		comp->mPhysicalLocation.setValue( os.str( ), 90, __FILE__,
			__LINE__ );
		return true;

	}

	void DeviceTreeCollector::postProcess( Component* comp )
	{
		checkLocation( comp );

		if( comp->devBus.dataValue == "scsi" )
		{
			string physical = "", logical = comp->mPhysicalLocation.getValue( );
			Component* parent = comp->mpParent;
			if( comp->mSecondLocation.dataValue != "" )
			{
				ostringstream val;
				if (parent->mPhysicalLocation.dataValue != "")
					val << parent->mPhysicalLocation.dataValue;
				else
					val << getAttrValue( parent->deviceTreeNode.dataValue,
										 "ibm,loc-code" );
				val << "-D" << comp->mSecondLocation.dataValue;
				physical = val.str( );
			}

			if( physical != "" )
			{
				comp->mPhysicalLocation.setValue( physical, 70, __FILE__,
					__LINE__ );
				comp->mSecondLocation.setValue( logical, 90, __FILE__,
					__LINE__ );
			}
		}

		vector<Component*>::iterator i, end;
		for( i = comp->mLeaves.begin( ), end = comp->mLeaves.end( ); i != end;
			++i )
		{
			postProcess( (*i) );
		}
	}

	string DeviceTreeCollector::resolveClassPath( const string& path )
	{
		return "";
	}
}
