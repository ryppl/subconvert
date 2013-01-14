#!/bin/bash -x

set -o errexit

WORKSPACE="$(pwd)"
RAMDISK="$WORKSPACE/ramdisk"  # ideally this really *would* be on a ramdisk
SUBCONVERT="$WORKSPACE/../subconvert"
SCRIPTS="$SUBCONVERT/subconvert/bin"
DOC="$SUBCONVERT/subconvert/doc"

#(cd "$WORKSPACE/mirrors/modularized"
#    && git status --porcelain | awk '{print $2}' | xargs rm -fr)

#(cd boost-git; git reset --hard HEAD; git pull; \
# git submodule foreach "git reset --hard HEAD"; \
# git submodule update --init)
# (cd boost-private; git pull)
# (cd boost-svn; git pull)
# (cd Boost.Defrag; git pull)
# (cd installer; git pull)
# (cd ryppl; git pull)
# (cd boost-modularize; git pull)

# perl -i -pe "s%url =.*%url = file://$PWD/boost.svnrepo%;" boost-clone/.git/config
# (cd boost-clone; git svn fetch; git reset --hard trunk)

mkdir -p $RAMDISK/cpp

if [[ -d $RAMDISK/cpp ]]; then
    /bin/rm -fr $RAMDISK/cpp
    mkdir $RAMDISK/cpp
    cd $RAMDISK/cpp

    git init
    
    export LD_LIBRARY_PATH="$SUBCONVERT/prefix/lib"

    "$SUBCONVERT/prefix/bin/subconvert" --verbose --debug\
           -A "$DOC/authors.txt"                         \
           -B "$DOC/branches.txt"                        \
           convert /home/svnsync/dump/boost.svndump

    git symbolic-ref HEAD refs/heads/trunk
    git prune
    sleep 5

    git remote add origin git@github.com:ryppl/boost-history.git
    git push -f --all origin
    git push -f --mirror origin
    git push -f --tags origin

    sleep 5
    rsync -av --delete .git/ $WORKSPACE/boost-history.git/

    cd $WORKSPACE
    # sudo umount $RAMDISK
    rm -fr $RAMDISK
    fi
fi

echo "Done!"
