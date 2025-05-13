#!/bin/bash

FILE=build/bin/yuzu
if test -f "$FILE"; then
    # remove any previously made AppImage in the base torzu git folder
    rm ./torzu.AppImage

    # enter AppImage utility folder
    cd AppImageBuilder

    # run the build script to create the AppImage
    # (usage) ./build.sh [source torzu build folder] [destination .AppImage file]
    ./build.sh ../build ./torzu.AppImage

    FILE=./torzu.AppImage
    if test -f "$FILE"; then
       # move the AppImage to the main torzu folder
       mv torzu.AppImage ..
       # return to main torzu folder
       cd ..
       # show contents of current folder
       echo
       ls
       # show AppImages specifically
       echo
       ls *.AppImage
       echo
       echo "'torzu.AppImage' is now located in the current folder."
       echo
    else
       cd ..
       echo "AppImage was not built."
    fi
else
    echo
    echo "$FILE does not exist."
    echo
    echo "No yuzu executable found in the /torzu/build/bin folder!"
    echo
    echo "You must first build a native linux version of torzu before running this script!"
    echo
fi
