#!/bin/bash
#
# Build script for Generating MongoDB 2.6.1 Debian (.deb) Packages
#
#Sam Caldwell
#Sr. DevOps Engineer
#Compare Metrics
#(sam@comparemetrics.com)
#
# Assumptions:
# 	1. You have an isolated Debian machine (virtual machines are great)
#	2. At present this is a minimal install environment with root login.
#
export SHORT_NAME="mongoDB-"
export DEB_PKG_NAME="mongoDB-PACKAGE_NAME-x64-2.6.1-ssl.deb"
export PACKAGES="server client mongos utils"
export SRC_NAME="mongodb-src-r2.6.1"
export SRC_FILE="$SRC_NAME.tar.gz"
export SRC_URL="https://fastdl.mongodb.org/src/$SRC_FILE"
export BUILD_DIR=~/build/mongodb-2.6.1-x86_64
export PACKAGE_DIR=~/package
#
apt-get install sudo gnupg2 scons build-essential libboost-filesystem-dev -y
apt-get install libboost-program-options-dev libboost-system-dev libboost-thread-dev -y
apt-get install git-core build-essential libssl-dev devscripts debhelper python-pymongo -y
apt-get install python-setuptools libpcre3-dev dpkg-dev rpm fakeroot createrepo  -y
easy_install pip
#
# 	3. Create the environment.
#
[ -d $BUILD_DIR ] && rm -rf $BUILD_DIR
[ -d $PACKAGE_DIR ] && rm -rf $PACKAGE_DIR

mkdir -p $BUILD_DIR
mkdir -p $PACKAGE_DIR

#
# 	4. Download the source code.
#
wget $SRC_URL || echo "failed to download source" && exit 1
tar -xvzf $SRC_FILE || echo "failed to untar $SRC_FILE" && exit 1
cd $SRC_NAME
#
# Compile the source
#
time scons -j 2 --ssl --64 --release --mute --prefix=~/binaries all
[ ! -d ./debian ] && echo "missing directory (debian)" && exit 1
[ ! -f ./debian/changelog ] && echo "missing asset (debian/changelog)" && exit 1
[ ! -f ./debian/compat ] && echo "missing asset (debian/compat)" && exit 1
[ ! -f ./debian/copyright ] && echo "missing asset (debian/copyright)" && exit 1
[ ! -f ./debian/dirs ] && echo "missing asset (debian/dirs)" && exit 1
[ ! -f ./debian/files ] && echo "missing asset (debian/files)" && exit 1
[ ! -f ./debian/preinst ] && echo "missing asset (debian/preinst)" && exit 1
[ ! -f ./debian/postrm ] && echo "missing asset (debian/postrm)" && exit 1
[ ! -f ./debian/prerm ] && echo "missing asset (debian/changelog)" && exit 1
[ ! -f ./debian/mongodb-org.control ] && echo "missing asset (debian/mongodb-org.control)" && exit 1
[ ! -f ./debian/mongodb-org.rules ] && echo "missing asset (debian/mongodb-org.rules)" && exit 1
[ ! -f ./debian/mongodb-org-server.postinst ] && echo "missing asset (debian/mongodb-org-server.postinst)" && exit 1
[ ! -f ./mongo ] && echo "missing asset (mongo)" && exit 1
[ ! -f ./mongobridge ] && echo "missing asset (mongobridge)" && exit 1
[ ! -f ./mongod ] && echo "missing asset (mongod)" && exit 1
[ ! -f ./mongodump ] && echo "missing asset (mongodump)" && exit 1
[ ! -f ./mongoexport ] && echo "missing asset (mongoexport)" && exit 1
[ ! -f ./mongofiles ] && echo "missing asset (mongofiles)" && exit 1
[ ! -f ./mongoimport ] && echo "missing asset (mongoimport)" && exit 1
[ ! -f ./mongooplog ] && echo "missing asset (mongooplog)" && exit 1
[ ! -f ./mongoperf ] && echo "missing asset (mongoperf)" && exit 1
[ ! -f ./mongorestore  ] && echo "missing asset (mongorestore)" && exit 1
[ ! -f ./mongos ] && echo "missing asset (mongos)" && exit 1
[ ! -f ./mongostat ] && echo "missing asset (mongostat)" && exit 1
[ ! -f ./mongotop ] && echo "missing asset (mongotop)" && exit 1
[ ! -f ./debian/init.d ] && echo "missing asset (debian/init.d)" && exit 1
[ ! -f ./debian/mongod.conf ] && echo "missing asset (debian/mongod.conf)" && exit 1
#
# Add more asset verifications here.
#
echo "All required assets have been verified."
#
# Tarball the package assets
#
for pkg in $(echo $PACKAGES); do
	[ -d $PACKAGE_DIR/$pkg ] && rm -rf $PACKAGE_DIR/$pkg
	mkdir -p $PACKAGE_DIR/$pkg/{DEBIAN,/etc/init.d,usr/bin}
	
	cp -v ./debian/changelog $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/compat $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/copyright $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/dirs $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/files $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/preinst $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/postrm $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/prerm $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/mongodb-org.control $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/mongodb-org.rules $PACKAGE_DIR/$pkg/DEBIAN/
	cp -v ./debian/mongodb-org-server.postinst $PACKAGE_DIR/$pkg/DEBIAN
done
	
cp -v ./mongo $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongobridge $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongod $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongodump $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongoexport $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongofiles $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongoimport $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongooplog $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongoperf $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongorestore $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongos $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongostat $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./mongotop $PACKAGE_DIR/mongodb-server/usr/bin
cp -v ./debian/init.d $PACKAGE_DIR/mongodb-server/etc/init.d
cp -v ./debian/mongod.conf $PACKAGE_DIR/mongodb-server/etc

cp -v ./mongo $PACKAGE_DIR/mongodb-client/usr/bin
cp -v ./mongobridge $PACKAGE_DIR/mongodb-client/usr/bin

cp -v ./mongo $PACKAGE_DIR/mongos/usr/bin
cp -v ./mongobridge $PACKAGE_DIR/mongos/usr/bin
cp -v ./mongos $PACKAGE_DIR/mongos/usr/bin
cp -v ./debian/init.d $PACKAGE_DIR/mongos/etc/init.d
cp -v ./debian/mongod.conf $PACKAGE_DIR/mongos/etc

cp -v ./mongo $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongodump $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongofiles $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongoimport $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongooplog $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongoperf $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongorestore $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongostat $PACKAGE_DIR/mongodb-utils/usr/bin
cp -v ./mongotop $PACKAGE_DIR/mongodb-utils/usr/bin

for pkg in $(echo $PACKAGES); do
	cd $PACKAGE_DIR/
	echo "Saving the build tree to ~/"
	tar -cvzf ~/$(echo $SRC_NAME | sed -e "s/src/$pkg/" )-binaries.tar.gz *
done

#
# Stopping here for testing
# 
#for pkg in $(echo $PACKAGES); do
#	cd $PACKAGE_DIR/$pkg
#	fakeroot dpkg-deb -b $SHORT_NAME-$pkg $DEB_PKG_NAME || ( echo "failed." && exit 1 )
#done

