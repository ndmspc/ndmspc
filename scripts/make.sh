#!/bin/bash

PROJECT_DIR="$(dirname $(dirname $(readlink -m $0)))"

USING_CLANG=false
BEING_STRICT=false

BUILDING_DOC=false
RUNNING_TEST=false

PRINT_DEBUG=false
MY_CMAKE_OPTS=""
MY_MAKE_OPTS=""
MY_BUILDSYS="make"

CXX=""
CC=""

for ARG in $@
do
	case $ARG in
		"clean")
			echo "Cleaning up build directory"
			rm -rf ${PROJECT_DIR}/build
			;;

		"doc")
			BUILDING_DOC=true
			;;

		"release")
			echo "Building release version"
			MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DCMAKE_BUILD_TYPE=RelWithDebInfo"
			BUILDING_DOC=true;
			# RUNNING_TEST=true;
			;;

		"ninja")
			which ninja &>/dev/null
			if [[ $? != 0 ]]
			then
				# Maybe we should fail the build?
				echo "Ninja build system specified, but not found! Falling back to make..."
			else
				echo "Using Ninja build system"
				MY_BUILDSYS=$(which ninja)
				MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -GNinja"
			fi
			;;

		"clang")
			which clang &>/dev/null
			if [[ $? != 0 ]]
			then
				echo "Clang compiler specified, but not found! Exiting..."
				exit 2
			fi

			which clang++ &>/dev/null
			if [[ $? != 0 ]]
			then
				echo "Clang++ compiler specified, but not found! Exiting..."
				exit 2
			fi

			echo "Using Clang compiler"

			CC=$(which clang)
			CXX=$(which clang++)
			USING_CLANG=true
			;;

		"strict")
			echo "Enabling strict mode (-Weverything)"
			[[ $USING_CLANG == false ]] && echo "Warning: this only works with Clang compiler!"
			BEING_STRICT=true
			MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWARN_EVERYTHING:bool=ON"
			;;

		"check_")
			echo "Enabling checking mode (use with release)."
			echo "WARNING: Use of this parameter in production is STRICTLY FORBIDDEN!!!"
			MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWARN_CHECK:bool=ON"
			;;

		"debug_")
			PRINT_DEBUG=true
			;;

		"install")
			echo "Will install after compilation"
			MY_MAKE_OPTS="${MY_MAKE_OPTS} install"
			;;
		"rpm")
			echo "Building rpm"
			MY_MAKE_OPTS="${MY_MAKE_OPTS} rpm"
			;;
		"test")
			echo "Running tests"
			RUNNING_TEST=true;
			;;

		*)
			echo "Unknown argument! Exiting..."
			exit 1
	esac
done

if [[ $BEING_STRICT == true && $USING_CLANG == false ]]
then
	echo "Cannot use strict mode without Clang compiler! Exiting..."
	exit 2
fi

if [[ $BUILDING_DOC == true ]]
then
	echo "Building with documentation"
	MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DBUILD_DOCUMENTATION:bool=ON"
fi

if [[ ! -d $PROJECT_DIR/build ]]
then
	echo "Creating build directory"
	mkdir $PROJECT_DIR/build
fi
MY_PROG_NAME="ndmspc"
if [ -n "$MY_PROJECT_VER" ];then
        MY_VER_MAJOR=$(echo "$MY_PROJECT_VER" | cut -d '.' -f1)
        MY_VER_MINOR=$(echo "$MY_PROJECT_VER" | cut -d '.' -f2)
        MY_VER_PATCH=$(echo "$MY_PROJECT_VER" | cut -d '.' -f3 | cut -d '-' -f1)
        MY_VER_RELEASE=$(echo "$MY_PROJECT_VER" | cut -sd '-' -f2)
        [ -n "$MY_VER_RELEASE" ] || MY_VER_RELEASE="1"
		[[ $MY_VER_RELEASE =~ ^[[:digit:]] ]] || MY_TWEAK_RELEASE=".0"
        [[ $MY_VER_RELEASE =~ ^[[:digit:]] ]] || MY_VER_RELEASE="0.1.$MY_VER_RELEASE"
        sed -i 's/^project(.*/project('$MY_PROG_NAME' VERSION '$MY_VER_MAJOR'.'$MY_VER_MINOR'.'$MY_VER_PATCH''$MY_TWEAK_RELEASE' DESCRIPTION "NDMSPC")/' CMakeLists.txt
        sed -i 's/^set(PROJECT_VERSION_RELEASE.*/set(PROJECT_VERSION_RELEASE '$MY_VER_RELEASE')/' CMakeLists.txt
        echo "Custom version : $MY_VER_MAJOR.$MY_VER_MINOR.$MY_VER_PATCH-$MY_VER_RELEASE"
fi

cd $PROJECT_DIR/build || { echo "Missing build directory!"; exit 1; }

if [[ $PRINT_DEBUG == true ]]
then
	echo
	echo "Debug information:"
	echo "CC=\"${CC}\""
	echo "CXX=\"${CXX}\""
	echo "BEING_STRICT=\"${BEING_STRICT}\""
	echo "USING_CLANG=\"${USING_CLANG}\""
	echo "MY_CMAKE_OPTS=\"${MY_CMAKE_OPTS}\""
	echo "MY_BUILDSYS=\"${MY_BUILDSYS}\""
	echo "MY_MAKE_OPTS=\"${MY_MAKE_OPTS}\""
fi

echo "----------------------------------------------------------------------"

[[ ${USING_CLANG} == true ]] && export CC CXX
cmake -DCMAKE_INSTALL_PREFIX=${PROJECT_DIR} ${MY_CMAKE_OPTS} ../

${MY_BUILDSYS} -j$(nproc) ${MY_MAKE_OPTS}


if [[ $RUNNING_TEST == true ]]
then
	echo "Running tests"
	${MY_BUILDSYS} test
fi
