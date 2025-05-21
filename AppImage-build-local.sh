#!/bin/bash

FILE=build/bin/sumi
if test -f "$FILE"; then
    # remove any previously made AppImage in the base sumi git folder
    rm ./sumi.AppImage

    # enter AppImage utility folder
    cd AppImageBuilder

    # run the build script to create the AppImage
    # (usage) ./build.sh [source sumi build folder] [destination .AppImage file]
    ./build.sh ../build ./sumi.AppImage

    FILE=./sumi.AppImage
    if test -f "$FILE"; then
       # move the AppImage to the main sumi folder
       mv sumi.AppImage ..
       # return to main sumi folder
       cd ..
       # show contents of current folder
       echo
       ls
       # show AppImages specifically
       echo
       ls *.AppImage
       echo
       echo "'sumi.AppImage' is now located in the current folder."
       echo
    else
       cd ..
       echo "AppImage was not built."
    fi
else
    echo
    echo "$FILE does not exist."
    echo
    echo "No sumi executable found in the /sumi/build/bin folder!"
    echo
    echo "You must first build a native linux version of sumi before running this script!"
    echo
fi
