#!/bin/bash
set -e
set -o pipefail

PROJECT_DIR="$(dirname $(dirname $(readlink -m $0)))"

USING_CLANG=false
BEING_STRICT=false

BUILDING_DOC=false
WITH_TEST=${WITH_TEST-false}
WITH_SERVER=${WITH_SERVER-true}
WITH_LEGACY=${WITH_LEGACY-false}
WITH_PARQUET=${WITH_PARQUET-false}
WITH_OPENTELEMETRY=${WITH_OPENTELEMETRY-false}

PRINT_DEBUG=${PRINT_DEBUG-false}
MY_CMAKE_OPTS=""
MY_MAKE_OPTS=""
MY_BUILDSYS="make"
MY_CMAKE_BUILD_TYPE=${MY_CMAKE_BUILD_TYPE-"Debug"}

CXX=""
CC=""

for ARG in $@; do
  case $ARG in
    "clean")
      echo "Cleaning up build directory"
      rm -rf ${PROJECT_DIR}/build ${PROJECT_DIR}/include ${PROJECT_DIR}/lib* ${PROJECT_DIR}/bin
      ;;
    "doc")
      BUILDING_DOC=true
      ;;

    "release")
      echo "Building release version"
      MY_CMAKE_BUILD_TYPE=RelWithDebInfo
      #MY_CMAKE_BUILD_TYPE=Release
      MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DENABLE_STRICT_WARNINGS:bool=ON"
      BUILDING_DOC=true
      # RUNNING_TEST=true;
      ;;

    "ninja")
      which ninja &>/dev/null
      if [[ $? != 0 ]]; then
        # Maybe we should fail the build?
        echo "Ninja build system specified, but not found! Falling back to make..."
      else
        echo "Using Ninja build system"
        MY_BUILDSYS=$(which ninja)
        MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -GNinja"
      fi
      ;;

    "strict")
      echo "Enabling strict mode (-Weverything)"
      BEING_STRICT=true
      MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DENABLE_STRICT_WARNINGS:bool=ON"
      MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWARNINGS_AS_ERRORS:bool=ON"
      ;;

    "debug_")
      PRINT_DEBUG=true
      ;;

    "install")
      echo "Will install after compilation"
      MY_MAKE_OPTS="${MY_MAKE_OPTS} install"
      ;;
    "server")
      echo "Forcing build with Parquet support"
      WITH_SERVER=true
      ;;
    "parquet")
      echo "Forcing build with Parquet support"
      WITH_PARQUET=true
      ;;
    "rpm")
      echo "Building rpm"
      MY_MAKE_OPTS="${MY_MAKE_OPTS} rpm"
      WITH_TEST=false
      ;;
    "srpm")
      echo "Building srpm"
      MY_MAKE_OPTS="${MY_MAKE_OPTS} srpm"
      WITH_TEST=false
      ;;
    "test")
      echo "Building with tests ..."
      WITH_TEST=true
      ;;

    *)
      echo "Unknown argument! Exiting..."
      exit 1
      ;;
  esac
done

echo "MY_CMAKE_BUILD_TYPE=$MY_CMAKE_BUILD_TYPE"
MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DCMAKE_BUILD_TYPE=${MY_CMAKE_BUILD_TYPE}"

if [[ $BUILDING_DOC == true ]]; then
  echo "Building with documentation"
  MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DBUILD_DOCUMENTATION:bool=ON"
fi
if [[ $WITH_LEGACY == true ]]; then
  echo "Building with legacy support"
  MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DUSE_LEGACY:bool=ON"
fi
if [[ $WITH_TEST == true ]]; then
  echo "Building with testing support"
  MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWITH_TEST:bool=ON"
fi
if [[ $WITH_PARQUET == true ]]; then
  MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWITH_PARQUET:bool=ON"
fi
if [[ $WITH_OPENTELEMETRY == true ]]; then
  MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWITH_OPENTELEMETRY:bool=ON"
fi
if [[ $WITH_SERVER == true ]]; then
  MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DWITH_SERVER:bool=ON"
fi

# MY_CMAKE_OPTS="${MY_CMAKE_OPTS} -DCMAKE_EXPORT_COMPILE_COMMANDS=1"

if [[ ! -d $PROJECT_DIR/build ]]; then
  echo "Creating build directory"
  mkdir $PROJECT_DIR/build
fi
MY_PROG_NAME="ndmspc"
if [ -n "$MY_PROJECT_VER" ]; then
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

cd $PROJECT_DIR/build || {
  echo "Missing build directory!"
  exit 1
}

if [[ $PRINT_DEBUG == true ]]; then
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

if [[ $WITH_TEST == true ]]; then
  echo "Running tests ..."
  ${MY_BUILDSYS} test
fi
